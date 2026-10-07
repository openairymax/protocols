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

#include "langchain_adapter_internal.h"
#include "protocol_vendor_ids.h"

int langchain_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    if (!context || !msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "langchain_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    return umsg_to_json(umsg, out_data, out_size);
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

    return unified_decode(data, size, (unified_message_t *)out_msg, AIRY_PROTOCOL_OPENJIUWEN);
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
