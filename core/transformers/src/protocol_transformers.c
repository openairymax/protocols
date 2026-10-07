// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_transformers.c
 * @brief 开放标准消息转换机制（JSON-RPC / MCP / A2A）。
 *
 * 机制核仅内置开放标准转换器；厂商面转换器由装配层经端口
 * proto_catalog_transforms() 注入，遵循机制与策略分离
 * （见 0.1.19 架构方案 §4.7/§5.1）。
 */

#include "protocol_transformers.h"
#include "protocol_catalog.h"
#include "protocol_transform_inline.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"

#include <compat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Transform Context
 * ============================================================================ */

transform_context_t *transform_context_create(const char *src_proto, const char *tgt_proto)
{
    transform_context_t *ctx = AIRY_CALLOC(1, sizeof(transform_context_t));
    if (!ctx)
        return NULL;
    if (src_proto)
        AIRY_STRNCPY_TERM(ctx->source_protocol, src_proto, sizeof(ctx->source_protocol));
    if (tgt_proto)
        AIRY_STRNCPY_TERM(ctx->target_protocol, tgt_proto, sizeof(ctx->target_protocol));
    return ctx;
}

void transform_context_destroy(transform_context_t *ctx)
{
    AIRY_FREE(ctx);
}

/* ============================================================================
 * MCP
 * ============================================================================ */

int transformer_jsonrpc_to_mcp_request(const unified_message_t *source, unified_message_t *target,
                                       void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    AIRY_MEMSET(target, 0, sizeof(*target));
    target->protocol = PROTOCOL_CUSTOM;
    AIRY_STRNCPY_TERM(target->protocol_name, "mcp", sizeof(target->protocol_name));
    target->direction = MSG_TYPE_REQUEST;
    AIRY_STRNCPY_TERM(target->method, "tools/call", sizeof(target->method));

    transform_context_t *ctx = (transform_context_t *)context;
    if (ctx && ctx->trace_id[0]) {
        AIRY_STRNCPY_TERM(target->metadata.trace_id, ctx->trace_id,
                          sizeof(target->metadata.trace_id));
    }

    char mcp_params[4096] = {0};
    const char *name = NULL;
    const char *arguments = "{}";

    if (source->payload) {
        const char *p = strstr(source->payload, "\"name\"");
        if (p) {
            const char *colon = strchr(p, ':');
            if (colon) {
                const char *start = strchr(colon + 1, '"');
                if (start) {
                    start++;
                    const char *end = strchr(start, '"');
                    if (end) {
                        size_t len = end - start;
                        name = AIRY_STRNDUP(start, len);
                    }
                }
            }
        }
        const char *a = strstr(source->payload, "\"arguments\"");
        if (a) {
            const char *colon2 = strchr(a, ':');
            if (colon2) {
                arguments = colon2 + 1;
            }
        }
    }

    snprintf(mcp_params, sizeof(mcp_params), "{\"name\":\"%s\",\"arguments\":%s}",
             name ? name : "unknown", arguments);

    AIRY_FREE((void *)name);

    target->payload_size = strlen(mcp_params) + 1;
    target->payload = AIRY_STRDUP(mcp_params);

    return 0;
}

int transformer_mcp_to_jsonrpc_response(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc",
                  source->is_error ? MSG_TYPE_ERROR : MSG_TYPE_RESPONSE);
    envelope_meta(target, context);

    if (source->is_error) {
        target->error_code = source->error_code;
        snprintf(target->error_msg, sizeof(target->error_msg), "%s",
                 source->payload ? (const char *)source->payload : "MCP error");
    } else {
        char output_buf[8192] = {0};
        const char *content_text = "";
        if (source->payload) {
            const char *text_key = strstr((const char *)source->payload, "\"text\"");
            if (text_key) {
                const char *val_start = strchr(text_key + 5, '"');
                if (val_start) {
                    content_text = val_start + 1;
                    const char *val_end = strchr(content_text, '"');
                    if (val_end) {
                        size_t len = val_end - content_text;
                        char *tmp = AIRY_MALLOC(len + 1);
                        __builtin_memcpy(tmp, content_text, len);
                        tmp[len] = '\0';
                        snprintf(output_buf, sizeof(output_buf),
                                 "{\"output\":%s,\"status\":\"success\"}", tmp);
                        AIRY_FREE(tmp);
                        content_text = output_buf;
                    } else {
                        snprintf(output_buf, sizeof(output_buf),
                                 "{\"output\":%s,\"status\":\"success\"}",
                                 (const char *)source->payload);
                        content_text = output_buf;
                    }
                }
            } else {
                snprintf(output_buf, sizeof(output_buf), "{\"output\":%s,\"status\":\"success\"}",
                         (const char *)source->payload);
                content_text = output_buf;
            }
        } else {
            content_text = "{\"output\":\"\",\"status\":\"success\"}";
        }

        target->payload_size = strlen(content_text) + 1;
        target->payload = AIRY_STRDUP(content_text);
    }

    return 0;
}

int transformer_mcp_tools_list_to_jsonrpc(const unified_message_t *source,
                                          unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc", MSG_TYPE_RESPONSE);
    envelope_meta(target, context);
    payload_pass(target, source, "{\"skills\":[]}");

    return 0;
}

/* ============================================================================
 * A2A
 * ============================================================================ */

int transformer_jsonrpc_to_a2a_task(const unified_message_t *source, unified_message_t *target,
                                    void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_CUSTOM, "a2a", MSG_TYPE_REQUEST);
    AIRY_STRNCPY_TERM(target->method, "task/delegate", sizeof(target->method));

    transform_context_t *ctx = (transform_context_t *)context;
    if (ctx && ctx->agent_id[0]) {
        AIRY_STRNCPY_TERM(target->sender_id, ctx->agent_id, sizeof(target->sender_id));
    }
    if (ctx && ctx->trace_id[0]) {
        AIRY_STRNCPY_TERM(target->metadata.trace_id, ctx->trace_id,
                          sizeof(target->metadata.trace_id));
    }

    char a2a_payload[8192] = {0};
    const char *description = "";
    const char *input_data = "{}";

    if (source->payload) {
        const char *desc_key = strstr(source->payload, "\"description\"");
        if (desc_key) {
            const char *val_start = strchr(desc_key + 11, '"');
            if (val_start) {
                description = val_start + 1;
            }
        }
        const char *input_key = strstr(source->payload, "\"input\"");
        if (input_key) {
            const char *colon = strchr(input_key, ':');
            if (colon)
                input_data = colon + 1;
        }
    }

    snprintf(a2a_payload, sizeof(a2a_payload), "{\"description\":%s,\"input_data\":%s}",
             description ? description : "\"\"", input_data);

    target->payload_size = strlen(a2a_payload) + 1;
    target->payload = AIRY_STRDUP(a2a_payload);

    return 0;
}

int transformer_a2a_to_jsonrpc_response(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc", MSG_TYPE_RESPONSE);
    envelope_meta(target, context);
    payload_pass(target, source, "{\"result\":{}}");

    return 0;
}

int transformer_jsonrpc_to_a2a_discover(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    if (!target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_CUSTOM, "a2a/agent/discover", DIRECTION_REQUEST);

    transform_context_t *ctx = (transform_context_t *)context;
    if (ctx) {
        if (ctx->agent_id[0])
            AIRY_STRNCPY_TERM(target->sender_id, ctx->agent_id, sizeof(target->sender_id));
        if (ctx->trace_id[0])
            AIRY_STRNCPY_TERM(target->metadata.trace_id, ctx->trace_id,
                              sizeof(target->metadata.trace_id));
    }

    target->payload = AIRY_STRDUP("{}");
    target->payload_size = 3;

    return 0;
}

int transformer_a2a_agents_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                      void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc", DIRECTION_RESPONSE);
    envelope_meta(target, context);
    payload_pass(target, source, "{\"agents\":[]}");

    return 0;
}

/* ============================================================================
 * Auto-transform dispatcher
 * ============================================================================ */

static const proto_transform_def_t g_transform_table[] = {
    {"jsonrpc", "mcp", transformer_jsonrpc_to_mcp_request},
    {"mcp", "jsonrpc", transformer_mcp_to_jsonrpc_response},
    {"jsonrpc", "a2a", transformer_jsonrpc_to_a2a_task},
    {"a2a", "jsonrpc", transformer_a2a_to_jsonrpc_response},
    {NULL, NULL, NULL}};

static int dispatch_table(const proto_transform_def_t *table, const char *from, const char *to,
                          const unified_message_t *source, unified_message_t *target)
{
    if (!table)
        return AIRY_ERR_NOT_FOUND;

    for (int i = 0; table[i].from_proto != NULL; i++) {
        if (strcasecmp(table[i].from_proto, from) == 0 &&
            strcasecmp(table[i].to_proto, to) == 0) {
            transform_context_t *ctx = transform_context_create(from, to);
            int ret = table[i].transform(source, target, ctx);
            transform_context_destroy(ctx);
            return ret;
        }
    }

    return AIRY_ERR_NOT_FOUND;
}

int protocol_auto_transform(const unified_message_t *source, unified_message_t *target,
                            const char *target_protocol_name)
{
    if (!source || !target || !target_protocol_name)
        return AIRY_ERR_NULL_POINTER;

    const char *from = source->endpoint[0] ? source->endpoint : "jsonrpc";

    int ret = dispatch_table(g_transform_table, from, target_protocol_name, source, target);
    if (ret == AIRY_ERR_NOT_FOUND) {
        size_t count = 0;
        const proto_transform_def_t *vendor = proto_catalog_transforms(&count);
        (void)count;
        ret = dispatch_table(vendor, from, target_protocol_name, source, target);
    }

    if (ret == AIRY_ERR_NOT_FOUND) {
        __builtin_memcpy(target, source, sizeof(*target));
        return 0;
    }

    return ret;
}

int protocol_validate_transformed(const unified_message_t *msg)
{
    if (!msg)
        return AIRY_ERR_NULL_POINTER;

    if (msg->endpoint[0] == '\0')
        return AIRY_ERR_INVALID_PARAM;
    if (msg->payload == NULL && msg->payload_size > 0)
        return AIRY_ERR_INVALID_PARAM;
    if (msg->payload != NULL && msg->payload_size < 4)
        return AIRY_ERR_OUT_OF_MEMORY;

    return 0;
}
