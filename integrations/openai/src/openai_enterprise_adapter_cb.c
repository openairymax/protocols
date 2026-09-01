// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_cb.c
 * @brief OpenAI enterprise adapter protocol callback domain (codec/connection/transport/request handling).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/error.h"
#include "error.h"

#include <stdio.h>
#include <string.h>

int openai_adapter_init_cb(void *context)
{
    if (!context) {
        if (!g_openai_instance) {
            openai_enterprise_config_t default_cfg = {0};
            openai_handle_t handle = NULL;
            if (openai_create(default_cfg, &handle) == 0) {
                g_openai_instance = (struct openai_enterprise_adapter_s *)handle;
            }
        }
        return g_openai_instance ? 0 : -1;
    }
    return 0;
}

int openai_adapter_destroy_cb(void *context)
{
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)context;
    if (adapter) {
        openai_destroy((openai_handle_t)adapter);
    } else if (g_openai_instance) {
        openai_handle_t h = (openai_handle_t)g_openai_instance;
        openai_destroy(h);
        g_openai_instance = NULL;
    }
    return 0;
}

int openai_adapter_encode_cb(void *c, const void *m, void **o, size_t *s)
{
    if (!m || !o || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_adapter_encode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    (void)c;
    const char *msg = (const char *)m;
    size_t len = strlen(msg) + 1;
    char *buf = (char *)AIRY_MALLOC(len);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "strlen: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(buf, msg, len);
    *o = buf;
    *s = len;
    return 0;
}

int openai_adapter_decode_cb(void *c, const void *d, size_t s, void *o)
{
    if (!d || !o || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_adapter_decode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    (void)c;
    __builtin_memcpy(o, d, s);
    return 0;
}

int openai_adapter_connect_cb(void *c, const char *endpoint)
{
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (!endpoint) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (adapter) {
        if (adapter->config.base_url)
            AIRY_FREE(adapter->config.base_url);
        adapter->config.base_url = AIRY_STRDUP(endpoint);
        adapter->initialized = true;
    }
    return 0;
}

int openai_adapter_disconnect_cb(void *c)
{
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (adapter) {
        adapter->initialized = false;
    }
    return 0;
}

int openai_adapter_is_connected_cb(void *c)
{
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    return (adapter && adapter->initialized) ? 1 : 0;
}

int openai_adapter_send_cb(void *c, const void *d, size_t s)
{
    if (!d || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_adapter_send_cb: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (!adapter || !adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: not initialized");
        return AIRY_ERR_UNKNOWN;
    }
    adapter->request_counter++;
    adapter->stats_chat_completions++;
    return (int)s;
}

int openai_adapter_receive_cb(void *c, void **d, size_t *s, uint32_t t)
{
    if (!d || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_adapter_receive_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (!adapter || !adapter->initialized) {
        *d = NULL;
        *s = 0;
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    if (adapter->last_response_body && adapter->last_response_len > 0) {
        size_t len = adapter->last_response_len;
        char *buf = (char *)AIRY_MALLOC(len + 1);
        if (!buf) {
            *d = NULL;
            *s = 0;
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "openai: out of memory");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        __builtin_memcpy(buf, adapter->last_response_body, len);
        buf[len] = '\0';
        *d = buf;
        *s = len;
        return 0;
    }

    (void)t;
    *d = NULL;
    *s = 0;
    return 0;
}

int openai_adapter_handle_request_cb(void *c, const void *r, void **rp)
{
    if (!r) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_adapter_handle_request_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (!adapter || !adapter->initialized) {
        if (rp)
            *rp = NULL;
        return AIRY_EINVAL;
    }
    adapter->request_counter++;

    const char *request_json = (const char *)r;
    char *response_json = NULL;
    struct openai_enterprise_context_s ctx_local;
    AIRY_MEMSET(&ctx_local, 0, sizeof(ctx_local));
    ctx_local.handle = (openai_handle_t)adapter;
    int rc = openai_enterprise_route_request(&ctx_local, "/v1/chat/completions", "POST",
                                             request_json, &response_json);

    if (rc == 0 && response_json) {
        if (adapter->last_response_body) {
            AIRY_FREE(adapter->last_response_body);
        }
        adapter->last_response_len = strlen(response_json);
        adapter->last_response_body = AIRY_STRDUP(response_json);

        if (rp) {
            *rp = response_json;
        } else {
            AIRY_FREE(response_json);
        }
        return 0;
    }

    AIRY_FREE(response_json);
    if (rp)
        *rp = NULL;
    return rc != 0 ? rc : -1;
}

int openai_adapter_get_version_cb(void *c, char *b, size_t s)
{
    (void)c;
    snprintf(b, s, "%s", OPENAI_ADAPTER_VERSION);
    return 0;
}

uint32_t openai_adapter_capabilities_cb(void *c)
{
    (void)c;
    return 0x07;
}

int openai_adapter_get_stats_cb(void *c, char *b, size_t s)
{
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)c;
    if (!adapter)
        adapter = g_openai_instance;
    if (!adapter) {
        snprintf(b, s, "{}");
        return 0;
    }
    snprintf(b, s,
             "{\"requests\":%lu,\"chat_completions\":%u,\"embeddings\":%u,"
             "\"input_tokens\":%lu,\"output_tokens\":%lu,"
             "\"avg_latency_ms\":%.2f,\"rate_429_count\":%u}",
             (unsigned long)adapter->request_counter, adapter->stats_chat_completions,
             adapter->stats_embeddings, (unsigned long)adapter->stats_total_input_tokens,
             (unsigned long)adapter->stats_total_output_tokens,
             adapter->stats_latency_count > 0 ?
                 adapter->stats_total_latency_ms / adapter->stats_latency_count :
                 0.0,
             adapter->rate_429_count);
    return 0;
}
