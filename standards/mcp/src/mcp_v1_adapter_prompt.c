// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_prompt.c
 * @brief MCP v1.0 prompt domain (prompts/list, prompts/get).
 */

#include "mcp_v1_adapter_internal.h"

#include "mcp_transport.h"
#include "airy_memory.h"
#include "error.h"
#include "types.h"
#include "unified_protocol.h"

#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logging.h"

int mcp_v1_handle_prompts_list(mcp_v1_context_t *ctx, char **response_json)
{
    if (!ctx || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_prompts_list: failed");
        return AIRY_ERR_UNKNOWN;
    }

    size_t buf_size = 4096 + ctx->prompt_count * 512;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "{\"prompts\":[");

    for (size_t i = 0; i < ctx->prompt_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        char *name = json_string_escape(ctx->prompts[i].prompt.name);
        char *desc = json_string_escape(ctx->prompts[i].prompt.description);
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"name\":%s,\"description\":%s,\"arguments\":%s}", name, desc,
                           ctx->prompts[i].prompt.arguments_schema_json ?
                               ctx->prompts[i].prompt.arguments_schema_json :
                               "[]");
        AIRY_FREE(name);
        AIRY_FREE(desc);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;
    return 0;
}

int mcp_v1_handle_prompts_get(mcp_v1_context_t *ctx, const char *name, const char *arguments_json,
                              char **response_json)
{
    if (!ctx || !name || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_prompts_get: failed");
        return AIRY_ERR_UNKNOWN;
    }

    mcp_prompt_entry_t *found = NULL;
    for (size_t i = 0; i < ctx->prompt_count; i++) {
        if (strcmp(ctx->prompts[i].prompt.name, name) == 0) {
            found = &ctx->prompts[i];
            break;
        }
    }

    if (!found || !found->handler) {
        AIRY_LOG_WARN("prompt not found or no handler: name=%s, prompt_count=%zu", name,
                 ctx->prompt_count);
        *response_json = AIRY_STRDUP("{\"description\":\"Prompt not found\",\"messages\":[]}");
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    mcp_sampling_message_t *messages = NULL;
    size_t message_count = 0;
    found->handler(name, arguments_json, &messages, &message_count, found->user_data);

    size_t buf_size = 4096 + message_count * 1024;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    char *name_esc = json_string_escape(name);
    offset += snprintf(buf + offset, buf_size - offset,
                       "{\"description\":\"Prompt: %s\",\"messages\":[", name_esc);
    AIRY_FREE(name_esc);
    name_esc = NULL;

    for (size_t i = 0; i < message_count && i < 100; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        char *role = json_string_escape(messages[i].role);
        char *content =
            json_string_escape(messages[i].content ? (const char *)messages[i].content : "");
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"role\":%s,\"content\":{\"type\":\"text\",\"text\":\"%s\"}}", role,
                           content);
        AIRY_FREE(role);
        AIRY_FREE(content);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;
    return 0;
}
