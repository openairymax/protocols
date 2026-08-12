// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file langchain_adapter_proto.c
 * @brief LangChain adapter protocol callback domain (codec/connection/send-recv/request handling).
 *
 * Single responsibility: implement the proto_adapter_t interface callbacks
 * (encode/decode/connect/disconnect/is_connected/send/receive/get_stats/
 * handle_request), mounted via langchain_get_protocol_adapter().
 */

#define LOG_TAG "langchain_adapter"

#include "langchain_adapter.h"
#include "langchain_adapter_internal.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int langchain_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    if (!context || !msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    const char *payload = umsg->payload ? (const char *)umsg->payload : "";
    size_t payload_len = umsg->payload_size;
    size_t buf_size = 256 + payload_len;
    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "langchain_proto_encode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    int written =
        snprintf(buf, buf_size,
                 "{\"protocol\":%d,\"direction\":%d,\"timestamp\":%llu,\"payload\":\"%.*s\"}",
                 (int)umsg->protocol, (int)umsg->direction, (unsigned long long)umsg->timestamp,
                 (int)payload_len, payload);
    if (written < 0 || (size_t)written >= buf_size) {
        AIRY_FREE(buf);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "langchain_proto_encode: snprintf failed");
        return AIRY_ERR_IO;
    }
    *out_data = buf;
    *out_size = (size_t)written;
    return 0;
}

int langchain_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    if (!context || !data || !out_msg) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (size == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
    }

    unified_message_t *msg = (unified_message_t *)out_msg;
    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "langchain_proto_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
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

int langchain_proto_connect(void *context, const char *endpoint)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_connect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    ctx->is_connected = true;
    return 0;
}

int langchain_proto_disconnect(void *context)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_disconnect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    ctx->is_connected = false;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = NULL;
    return 0;
}

int langchain_proto_is_connected(void *context)
{
    if (!context)
        return 0;
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    return ctx->is_connected ? 1 : 0;
}

int langchain_proto_send(void *context, const void *data, size_t size)
{
    if (!context || !data) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    AIRY_FREE(ctx->send_buffer);
    ctx->send_buffer = AIRY_MALLOC(size + 1);
    if (!ctx->send_buffer) {
        ctx->send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "langchain_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(ctx->send_buffer, data, size);
    ((char *)ctx->send_buffer)[size] = '\0';
    ctx->send_buffer_size = size;
    ctx->bytes_sent += size;
    return 0;
}

int langchain_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!context || !data || !size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    if (!ctx->send_buffer || ctx->send_buffer_size == 0) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__,
                         "langchain_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
    }
    *data = ctx->send_buffer;
    *size = ctx->send_buffer_size;
    ctx->bytes_received += *size;
    ctx->send_buffer = NULL;
    ctx->send_buffer_size = 0;
    return 0;
}

int langchain_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    if (!context || !stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    int written =
        snprintf(stats_json, max_size,
                 "{\"adapter\":\"langchain\",\"version\":\"%s\",\"connected\":%s,"
                 "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                 "\"requests\":%llu,\"tools\":%zu}",
                 LANGCHAIN_ADAPTER_VERSION, ctx->is_connected ? "true" : "false",
                 (unsigned long long)ctx->bytes_sent, (unsigned long long)ctx->bytes_received,
                 (unsigned long long)ctx->total_chains_executed, ctx->tool_count);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
}

int langchain_proto_handle_request(void *context, const void *req, void **resp)
{
    if (!context || !req || !resp)
        return AIRY_ERR_NULL_POINTER;

    langchain_adapter_context_t *ctx = (langchain_adapter_context_t *)context;
    const unified_message_t *msg = (const unified_message_t *)req;
    const char *raw_request = (const char *)(msg->payload ? msg->payload : "{}");

    char agent_id[64] = "proto-agent";
    langchain_execution_result_t result = {0};
    int ret = langchain_agent_run(ctx, agent_id, raw_request, &result);

    if (ret == 0 && result.output_json) {
        *resp = AIRY_STRDUP(result.output_json);
    } else {
        *resp = AIRY_STRDUP("{\"status\":\"error\"}");
        ret = -1;
    }

    langchain_execution_result_destroy(&result);
    return ret;
}
