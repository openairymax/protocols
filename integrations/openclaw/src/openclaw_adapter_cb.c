// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_cb.c
 * @brief OpenClaw adapter protocol callback domain (proto_adapter_t interface implementation).
 */

#define LOG_TAG "openclaw_adapter"

#include "openclaw_adapter.h"
#include "openclaw_adapter_internal.h"

#include "protocol_transformers.h"

#include <stdio.h>
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "airy_memory.h"
#include "types.h"

int openclaw_proto_init(void *context)
{
    openclaw_config_t config = openclaw_config_default();
    openclaw_adapter_context_t *ctx = openclaw_adapter_create(&config);
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_proto_init: failed");
        return AIRY_ERR_UNKNOWN;
    }
    *(void **)context = ctx;
    return 0;
}

int openclaw_proto_destroy(void *context)
{
    if (context) {
        openclaw_adapter_destroy((openclaw_adapter_context_t *)context);
    }
    return 0;
}

int openclaw_proto_handle_request(void *context, const void *req, void **resp)
{
    if (!context || !req) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_proto_handle_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;

    const char *raw_request = (const char *)req;
    openclaw_message_t msg = {0};
    msg.message_id = "proto-req";
    msg.payload = (void *)raw_request;
    msg.payload_size = raw_request ? strlen(raw_request) : 0;
    msg.modality = OPENCLAW_MODALITY_TEXT;
    msg.timestamp = (uint64_t)(time(NULL));

    openclaw_message_t response = {0};
    int ret = openclaw_send_message(ctx, &msg, &response);

    if (resp) {
        if (ret == 0 && response.payload && response.payload_size > 0) {
            *resp = AIRY_MALLOC(response.payload_size + 1);
            if (*resp) {
                __builtin_memcpy(*resp, response.payload, response.payload_size);
                ((char *)*resp)[response.payload_size] = '\0';
            }
        } else if (ret == 0) {
            char stats_buf[2048] = {0};
            openclaw_get_statistics(ctx, stats_buf, sizeof(stats_buf));
            *resp = AIRY_STRDUP(stats_buf);
        } else {
            char err_buf[512];
            int err_len = snprintf(
                err_buf, sizeof(err_buf),
                "{\"error\":\"Request processing failed\",\"code\":%d,\"adapter_version\":\"%s\"}",
                ret, OPENCLAW_ADAPTER_VERSION);
            if (err_len > 0 && (size_t)err_len < sizeof(err_buf)) {
                *resp = AIRY_STRDUP(err_buf);
            } else {
                *resp = NULL;
            }
        }
    }

    openclaw_message_destroy(&msg);
    openclaw_message_destroy(&response);
    return ret;
}

int openclaw_proto_get_version(void *context, char *buf, size_t max_size)
{
    if (!buf || max_size == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_proto_get_version: failed");
        return AIRY_ERR_UNKNOWN;
    }
    const char *ver = openclaw_adapter_version();
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, ver, len);
    buf[len] = '\0';
    return 0;
}

uint32_t openclaw_proto_capabilities(void *context)
{
    return (uint32_t)(PROTO_CAP_MULTIMODAL | PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING |
                      PROTO_CAP_AGENT_DISCOVERY);
}

int openclaw_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    if (!context || !msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    const char *payload = umsg->payload ? (const char *)umsg->payload : "";
    size_t payload_len = umsg->payload_size;
    size_t buf_size = 256 + payload_len;
    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openclaw_proto_encode: oom");
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
                         "openclaw_proto_encode: snprintf failed");
        return AIRY_ERR_IO;
    }
    *out_data = buf;
    *out_size = (size_t)written;
    return 0;
}

int openclaw_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    if (!context || !data || !out_msg) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (size == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
    }

    unified_message_t *msg = (unified_message_t *)out_msg;
    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openclaw_proto_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    msg->protocol = AIRY_PROTOCOL_OPENCLAW;
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

int openclaw_proto_connect(void *context, const char *endpoint)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_connect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    ctx->connected = true;
    return 0;
}

int openclaw_proto_disconnect(void *context)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_disconnect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    ctx->connected = false;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = NULL;
    return 0;
}

int openclaw_proto_is_connected(void *context)
{
    if (!context)
        return 0;
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    return ctx->connected ? 1 : 0;
}

int openclaw_proto_send(void *context, const void *data, size_t size)
{
    if (!context || !data) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    AIRY_FREE(ctx->send_buffer);
    ctx->send_buffer = AIRY_MALLOC(size + 1);
    if (!ctx->send_buffer) {
        ctx->send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openclaw_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(ctx->send_buffer, data, size);
    ((char *)ctx->send_buffer)[size] = '\0';
    ctx->send_buffer_size = size;
    ctx->bytes_sent += size;
    return 0;
}

int openclaw_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!context || !data || !size) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    if (!ctx->send_buffer || ctx->send_buffer_size == 0) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__,
                         "openclaw_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
    }
    *data = ctx->send_buffer;
    *size = ctx->send_buffer_size;
    ctx->bytes_received += *size;
    ctx->send_buffer = NULL;
    ctx->send_buffer_size = 0;
    return 0;
}

int openclaw_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    if (!context || !stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "openclaw_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }
    openclaw_adapter_context_t *ctx = (openclaw_adapter_context_t *)context;
    int written =
        snprintf(stats_json, max_size,
                 "{\"adapter\":\"openclaw\",\"version\":\"%s\",\"connected\":%s,"
                 "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                 "\"messages_sent\":%llu,\"messages_received\":%llu,"
                 "\"agents\":%zu,\"sessions\":%zu}",
                 OPENCLAW_ADAPTER_VERSION, ctx->connected ? "true" : "false",
                 (unsigned long long)ctx->bytes_sent, (unsigned long long)ctx->bytes_received,
                 (unsigned long long)ctx->messages_sent, (unsigned long long)ctx->messages_received,
                 ctx->registered_agent_count, ctx->active_session_count);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
}
