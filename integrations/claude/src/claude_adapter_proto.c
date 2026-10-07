// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file claude_adapter_proto.c
 * @brief Claude adapter protocol callback domain (codec/connection/send-recv/statistics).
 *
 * Single responsibility: implement the data-plane callbacks of the
 * proto_adapter_t interface (encode/decode/connect/disconnect/is_connected/
 * send/receive/get_stats), mounted via claude_get_protocol_adapter().
 */

#include "claude_adapter_internal.h"
#include "protocol_vendor_ids.h"

int claude_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    if (!context || !msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    return umsg_to_json(umsg, out_data, out_size);
}

int claude_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    if (!context || !data || !out_msg) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (size == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
    }

    return unified_decode(data, size, (unified_message_t *)out_msg, AIRY_PROTOCOL_CLAUDE);
}

int claude_proto_connect(void *context, const char *endpoint)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_connect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    ctx->is_connected = true;
    return 0;
}

int claude_proto_disconnect(void *context)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_disconnect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    ctx->is_connected = false;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = NULL;
    return 0;
}

int claude_proto_is_connected(void *context)
{
    if (!context)
        return 0;
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    return ctx->is_connected ? 1 : 0;
}

int claude_proto_send(void *context, const void *data, size_t size)
{
    if (!context || !data) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    AIRY_FREE(ctx->send_buffer);
    ctx->send_buffer = AIRY_MALLOC(size + 1);
    if (!ctx->send_buffer) {
        ctx->send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "claude_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(ctx->send_buffer, data, size);
    ((char *)ctx->send_buffer)[size] = '\0';
    ctx->send_buffer_size = size;
    ctx->bytes_sent += size;
    return 0;
}

int claude_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!context || !data || !size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    if (!ctx->send_buffer || ctx->send_buffer_size == 0) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__,
                         "claude_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
    }
    *data = ctx->send_buffer;
    *size = ctx->send_buffer_size;
    ctx->bytes_received += *size;
    ctx->send_buffer = NULL;
    ctx->send_buffer_size = 0;
    return 0;
}

int claude_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    if (!context || !stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "claude_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    int written =
        snprintf(stats_json, max_size,
                 "{\"adapter\":\"claude\",\"version\":\"%s\",\"connected\":%s,"
                 "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                 "\"total_requests\":%llu,\"total_tokens_in\":%llu,"
                 "\"total_tokens_out\":%llu,\"total_tool_calls\":%llu}",
                 CLAUDE_ADAPTER_VERSION, ctx->is_connected ? "true" : "false",
                 (unsigned long long)ctx->bytes_sent, (unsigned long long)ctx->bytes_received,
                 (unsigned long long)ctx->total_requests, (unsigned long long)ctx->total_tokens_in,
                 (unsigned long long)ctx->total_tokens_out,
                 (unsigned long long)ctx->total_tool_calls);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
}
