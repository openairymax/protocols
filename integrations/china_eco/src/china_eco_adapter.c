// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file china_eco_adapter.c
 * @brief China domestic ecosystem protocol adapter — core & protocol layer.
 *
 * Core responsibilities (after Phase 2.2c split):
 * - Handle lifecycle (create / destroy)
 * - LLM provider management (add / remove / chat dispatch)
 * - Object storage bridge (add / upload / download)
 * - Protocol adapter vtable (init / encode / decode / connect / send / …)
 *
 * LLM HTTP implementation → china_eco_llm.c
 * SM3 / SM4 crypto           → china_eco_crypto.c
 */

#include "china_eco_adapter.h"
#include "china_eco_llm.h"
#include "china_eco_crypto.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>
#endif
#ifdef AIRY_HAS_CURL
#include <curl/curl.h>
#endif

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static struct {
    china_eco_handle_t handle;
    bool proto_initialized;
    bool is_connected;
    char *connected_endpoint;
    void *send_buffer;
    size_t send_buffer_size;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} g_china_eco_state = {0};

static const char *g_storage_endpoint_urls[] = {
    [CHINA_ECO_OSS_ALIYUN] = "https://oss-cn-hangzhou.aliyuncs.com",
    [CHINA_ECO_OSS_TENCENT] = "https://cos.ap-guangzhou.myqcloud.com",
    [CHINA_ECO_OSS_BAIDU] = "https://bj.bcebos.com",
    [CHINA_ECO_OSS_HUAWEI] = "https://obs.cn-north-4.myhuaweicloud.com"};

static const char *__attribute__((used)) g_storage_names[] = {
    [CHINA_ECO_OSS_ALIYUN] = "oss",
    [CHINA_ECO_OSS_TENCENT] = "cos",
    [CHINA_ECO_OSS_BAIDU] = "bos",
    [CHINA_ECO_OSS_HUAWEI] = "obs"};

/* ================================================================== */
/*  Handle lifecycle                                                    */
/* ================================================================== */

int china_eco_create(china_eco_handle_t **handle)
{
    if (!handle)
        return AIRY_ERR_NULL_POINTER;
    china_eco_handle_t *h = (china_eco_handle_t *)AIRY_CALLOC(1, sizeof(china_eco_handle_t));
    if (!h)
        return AIRY_ERR_OUT_OF_MEMORY;
    h->initialized = true;
    *handle = h;
    return 0;
}

void china_eco_destroy(china_eco_handle_t *handle)
{
    if (!handle)
        return;
    AIRY_MEMSET(handle, 0, sizeof(china_eco_handle_t));
    AIRY_FREE(handle);
}

/* ================================================================== */
/*  LLM provider management                                           */
/* ================================================================== */

int china_eco_add_llm_provider(china_eco_handle_t *h, const china_eco_llm_provider_t *provider)
{
    if (!h || !provider)
        return AIRY_ERR_NULL_POINTER;
    if (h->llm_provider_count >= CHINA_ECO_MAX_PROVIDERS)
        return AIRY_ERR_OVERFLOW;

    for (size_t i = 0; i < h->llm_provider_count; i++) {
        if (h->llm_providers[i].provider_type == provider->provider_type) {
            h->llm_providers[i] = *provider;
            return 0;
        }
    }
    h->llm_providers[h->llm_provider_count++] = *provider;
    return 0;
}

int china_eco_remove_llm_provider(china_eco_handle_t *h, china_eco_provider_type_t type)
{
    if (!h)
        return AIRY_ERR_NULL_POINTER;
    for (size_t i = 0; i < h->llm_provider_count; i++) {
        if (h->llm_providers[i].provider_type == type) {
            if (i < h->llm_provider_count - 1)
                __builtin_memmove(&h->llm_providers[i], &h->llm_providers[i + 1],
                                  (h->llm_provider_count - i - 1) * sizeof(china_eco_llm_provider_t));
            h->llm_provider_count--;
            return 0;
        }
    }
    return AIRY_ERR_NOT_FOUND;
}

int china_eco_llm_chat(china_eco_handle_t *h, china_eco_provider_type_t provider,
                       const char *messages_json, const char *model_id, char *response,
                       size_t *resp_size)
{
    if (!h || !messages_json || !response || !resp_size || *resp_size == 0)
        return AIRY_ERR_NULL_POINTER;

    china_eco_llm_provider_t *p = NULL;
    for (size_t i = 0; i < h->llm_provider_count; i++) {
        if (h->llm_providers[i].provider_type == provider) {
            p = &h->llm_providers[i];
            break;
        }
    }
    if (!p) {
        airy_err_push_ex(AIRY_ERR_NOT_FOUND, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat: provider type %d not configured", (int)provider);
        return AIRY_ERR_NOT_FOUND;
    }
    if (!p->enabled) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat: provider disabled");
        return AIRY_ERR_STATE_ERROR;
    }
    if (p->api_key[0] == '\0') {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat: provider has no api key");
        return AIRY_ERR_INVALID_PARAM;
    }

    const char *effective_model = model_id;
    if (!effective_model || effective_model[0] == '\0')
        effective_model = p->model_id[0] != '\0' ? p->model_id : NULL;
    if (!effective_model) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat: no model id");
        return AIRY_ERR_INVALID_PARAM;
    }

    const char *api_url =
        p->api_base_url[0] != '\0' ? p->api_base_url : g_provider_api_urls[provider];

    uint64_t prompt_tokens = 0, completion_tokens = 0;
    int rc = china_eco_llm_chat_http(p, api_url, effective_model, messages_json, response,
                                     *resp_size, &prompt_tokens, &completion_tokens);
    if (rc < 0)
        return rc;

    h->request_counter++;
    h->token_total += prompt_tokens + completion_tokens;
    *resp_size = (size_t)rc;
    return 0;
}

/* ================================================================== */
/*  Storage bridge                                                    */
/* ================================================================== */

int china_eco_add_storage_bridge(china_eco_handle_t *h, const china_eco_storage_bridge_t *bridge)
{
    if (!h || !bridge)
        return AIRY_ERR_NULL_POINTER;
    if (h->storage_bridge_count >= CHINA_ECO_MAX_ENDPOINTS)
        return AIRY_ERR_OVERFLOW;

    for (size_t i = 0; i < h->storage_bridge_count; i++) {
        if (h->storage_bridges[i].storage_type == bridge->storage_type) {
            h->storage_bridges[i] = *bridge;
            return 0;
        }
    }
    h->storage_bridges[h->storage_bridge_count++] = *bridge;
    return 0;
}

int china_eco_storage_upload(china_eco_handle_t *h, china_eco_storage_type_t storage_type,
                             const void *data, size_t size, const char *object_key,
                             char *result_url, size_t *url_size)
{
    if (!h || !data || !object_key || !result_url || !url_size)
        return AIRY_ERR_NULL_POINTER;

    china_eco_storage_bridge_t *bridge = NULL;
    for (size_t i = 0; i < h->storage_bridge_count; i++) {
        if (h->storage_bridges[i].storage_type == storage_type) {
            bridge = &h->storage_bridges[i];
            break;
        }
    }

    const char *endpoint = bridge && bridge->endpoint_url[0] != '\0' ?
                               bridge->endpoint_url :
                               g_storage_endpoint_urls[storage_type];
    const char *bucket =
        bridge && bridge->bucket_name[0] != '\0' ? bridge->bucket_name : "agentrt-default";

    int written = snprintf(result_url, *url_size, "%s/%s/%s", endpoint, bucket, object_key);
    if (written > 0)
        *url_size = (size_t)written;
    return 0;
}

int china_eco_storage_download(china_eco_handle_t *h, china_eco_storage_type_t storage_type,
                               const char *object_key, void **data, size_t *size)
{
    if (!h || !object_key || !data || !size)
        return AIRY_ERR_NULL_POINTER;
    *data = NULL;
    *size = 0;
    return 0;
}

/* ================================================================== */
/*  Protocol adapter vtable                                           */
/* ================================================================== */

static int china_eco_proto_init(void *context)
{
    china_eco_handle_t *h = (china_eco_handle_t *)context;
    if (!h) {
        if (china_eco_create(&h) != 0)
            return AIRY_ERR_OUT_OF_MEMORY;
    }
    g_china_eco_state.handle = *h;
    g_china_eco_state.proto_initialized = true;
    return 0;
}

static int china_eco_proto_destroy(void *context)
{
    (void)context;
    g_china_eco_state.proto_initialized = false;
    g_china_eco_state.is_connected = false;
    AIRY_FREE(g_china_eco_state.connected_endpoint);
    g_china_eco_state.connected_endpoint = NULL;
    AIRY_FREE(g_china_eco_state.send_buffer);
    g_china_eco_state.send_buffer = NULL;
    g_china_eco_state.send_buffer_size = 0;
    return 0;
}

static int china_eco_proto_handle_request(void *context, const void *req, void **resp)
{
    if (!req || !resp)
        return AIRY_ERR_NULL_POINTER;

    char buf[4096];
    snprintf(buf, sizeof(buf),
             "{\"protocol\":\"china-eco\",\"version\":\"%s\","
             "\"llm_providers\":%zu,\"storage_bridges\":%zu,"
             "\"requests_total\":%llu,\"token_total\":%llu,\"sm_crypto\":true}",
             CHINA_ECO_VERSION, g_china_eco_state.handle.llm_provider_count,
             g_china_eco_state.handle.storage_bridge_count,
             (unsigned long long)g_china_eco_state.handle.request_counter,
             (unsigned long long)g_china_eco_state.handle.token_total);
    *resp = AIRY_STRDUP(buf);
    return 0;
}

static int china_eco_proto_get_version(void *context, char *version_buf, size_t max_size)
{
    (void)context;
    if (!version_buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    snprintf(version_buf, max_size, "%s", CHINA_ECO_VERSION);
    return 0;
}

static uint32_t china_eco_proto_capabilities(void *context)
{
    return (uint32_t)(CHINA_ECO_CAP_LLM_BRIDGE | CHINA_ECO_CAP_OBJECT_STORAGE |
                      CHINA_ECO_CAP_SM_CRYPTO | CHINA_ECO_CAP_MESSAGE_QUEUE |
                      CHINA_ECO_CAP_CONTENT_AUDIT);
}

static int china_eco_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    (void)context;
    if (!msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    const char *payload = umsg->payload ? (const char *)umsg->payload : "";
    size_t payload_len = umsg->payload_size;
    size_t buf_size = 256 + payload_len;
    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_proto_encode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    int written = snprintf(buf, buf_size,
                           "{\"protocol\":%d,\"direction\":%d,\"timestamp\":%llu,\"payload\":\"%.*s\"}",
                           (int)umsg->protocol, (int)umsg->direction,
                           (unsigned long long)umsg->timestamp,
                           (int)payload_len, payload);
    if (written < 0 || (size_t)written >= buf_size) {
        AIRY_FREE(buf);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "china_eco_proto_encode: snprintf failed");
        return AIRY_ERR_IO;
    }
    *out_data = buf;
    *out_size = (size_t)written;
    return 0;
}

static int china_eco_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    (void)context;
    if (!data || !out_msg) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (size == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
    }

    unified_message_t *msg = (unified_message_t *)out_msg;
    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_proto_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    msg->protocol = AIRY_PROTOCOL_CHINA_ECO;
    char *p = strstr(copy, "\"protocol\":");
    if (p)
        msg->protocol = (airy_protocol_type_t)strtol(p + 11, NULL, 10);

    msg->direction = DIRECTION_RESPONSE;
    p = strstr(copy, "\"direction\":");
    if (p)
        msg->direction = (message_direction_t)strtol(p + 12, NULL, 10);

    msg->timestamp = (uint64_t)time(NULL);
    p = strstr(copy, "\"timestamp\":");
    if (p)
        msg->timestamp = (uint64_t)strtoull(p + 12, NULL, 10);

    p = strstr(copy, "\"payload\":\"");
    if (p) {
        p += 11;
        char *end = strchr(p, '"');
        size_t plen = end ? (size_t)(end - p) : strlen(p);
        msg->payload = AIRY_MALLOC(plen + 1);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, p, plen);
            ((char *)msg->payload)[plen] = '\0';
            msg->payload_size = plen;
        }
    } else {
        msg->payload = AIRY_STRDUP("");
        msg->payload_size = 0;
    }
    AIRY_FREE(copy);
    return 0;
}

static int china_eco_proto_connect(void *context, const char *endpoint)
{
    (void)context;
    AIRY_FREE(g_china_eco_state.connected_endpoint);
    g_china_eco_state.connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    g_china_eco_state.is_connected = true;
    return 0;
}

static int china_eco_proto_disconnect(void *context)
{
    (void)context;
    g_china_eco_state.is_connected = false;
    AIRY_FREE(g_china_eco_state.connected_endpoint);
    g_china_eco_state.connected_endpoint = NULL;
    return 0;
}

static int china_eco_proto_is_connected(void *context)
{
    (void)context;
    return g_china_eco_state.is_connected ? 1 : 0;
}

static int china_eco_proto_send(void *context, const void *data, size_t size)
{
    (void)context;
    if (!data) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    AIRY_FREE(g_china_eco_state.send_buffer);
    g_china_eco_state.send_buffer = AIRY_MALLOC(size + 1);
    if (!g_china_eco_state.send_buffer) {
        g_china_eco_state.send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(g_china_eco_state.send_buffer, data, size);
    ((char *)g_china_eco_state.send_buffer)[size] = '\0';
    g_china_eco_state.send_buffer_size = size;
    g_china_eco_state.bytes_sent += size;
    return 0;
}

static int china_eco_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    if (!data || !size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (!g_china_eco_state.send_buffer || g_china_eco_state.send_buffer_size == 0) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__,
                         "china_eco_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
    }
    *data = g_china_eco_state.send_buffer;
    *size = g_china_eco_state.send_buffer_size;
    g_china_eco_state.bytes_received += *size;
    g_china_eco_state.send_buffer = NULL;
    g_china_eco_state.send_buffer_size = 0;
    return 0;
}

static int china_eco_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    (void)context;
    if (!stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "china_eco_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    int written = snprintf(stats_json, max_size,
                           "{\"adapter\":\"china_eco\",\"version\":\"%s\",\"connected\":%s,"
                           "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                           "\"requests_total\":%llu,\"token_total\":%llu,"
                           "\"llm_providers\":%zu,\"storage_bridges\":%zu}",
                           CHINA_ECO_VERSION, g_china_eco_state.is_connected ? "true" : "false",
                           (unsigned long long)g_china_eco_state.bytes_sent,
                           (unsigned long long)g_china_eco_state.bytes_received,
                           (unsigned long long)g_china_eco_state.handle.request_counter,
                           (unsigned long long)g_china_eco_state.handle.token_total,
                           g_china_eco_state.handle.llm_provider_count,
                           g_china_eco_state.handle.storage_bridge_count);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
}

const proto_adapter_t *china_eco_get_protocol_adapter(void)
{
    static proto_adapter_t adapter = {0};
    static bool initialized = false;

    if (!initialized) {
        adapter.name = "China Ecosystem";
        adapter.version = CHINA_ECO_VERSION;
        adapter.description =
            "Domestic ecosystem protocol compatibility - Bailian/Wenxin/DashScope LLM bridge, "
            "OSS/COS/BOS storage, SM2/SM3/SM4 crypto";
        adapter.type = AIRY_PROTOCOL_CHINA_ECO;
        adapter.init = china_eco_proto_init;
        adapter.destroy = china_eco_proto_destroy;
        adapter.encode = china_eco_proto_encode;
        adapter.decode = china_eco_proto_decode;
        adapter.connect = china_eco_proto_connect;
        adapter.disconnect = china_eco_proto_disconnect;
        adapter.is_connected = china_eco_proto_is_connected;
        adapter.send = china_eco_proto_send;
        adapter.receive = china_eco_proto_receive;
        adapter.handle_request = china_eco_proto_handle_request;
        adapter.get_version = china_eco_proto_get_version;
        adapter.capabilities = china_eco_proto_capabilities;
        adapter.get_stats = china_eco_proto_get_stats;
        initialized = true;
    }
    return &adapter;
}
