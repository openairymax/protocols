// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_tool.c
 * @brief MCP v1.0 tool domain (tools/list, tools/call and their streaming variants).
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

int mcp_v1_handle_tools_list(mcp_v1_context_t *ctx, char **response_json)
{
    if (!ctx || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_tools_list: failed");
        return AIRY_ERR_UNKNOWN;
    }

    size_t buf_size = 4096 + ctx->tool_count * 512;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "{\"tools\":[");

    for (size_t i = 0; i < ctx->tool_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        char *name = json_string_escape(ctx->tools[i].tool.name);
        char *desc = json_string_escape(ctx->tools[i].tool.description);
        offset +=
            snprintf(buf + offset, buf_size - offset,
                     "{\"name\":%s,\"description\":%s,\"inputSchema\":%s}", name, desc,
                     ctx->tools[i].tool.input_schema_json ? ctx->tools[i].tool.input_schema_json :
                                                            "{}");
        AIRY_FREE(name);
        AIRY_FREE(desc);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;
    return 0;
}

int mcp_v1_handle_tools_call(mcp_v1_context_t *ctx, const char *name, const char *arguments_json,
                             char **response_json)
{
    if (!ctx || !name || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_tools_call: failed");
        return AIRY_ERR_UNKNOWN;
    }

    mcp_tool_entry_t *found = NULL;
    for (size_t i = 0; i < ctx->tool_count; i++) {
        if (strcmp(ctx->tools[i].tool.name, name) == 0) {
            found = &ctx->tools[i];
            break;
        }
    }

    if (!found) {
        AIRY_LOG_WARN("tool not found: name=%s, tool_count=%zu", name, ctx->tool_count);
        char *name_esc = json_string_escape(name);
        const char *safe_name = name_esc ? name_esc : name;
        size_t len = snprintf(
            NULL, 0,
            "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"Tool not found: %s\"}]}",
            safe_name);
        char *resp = AIRY_MALLOC(len + 1);
        if (resp) {
            snprintf(resp, len + 1,
                     "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"Tool not found: "
                     "%s\"}]}",
                     safe_name);
        }
        AIRY_FREE(name_esc);
        *response_json = resp;
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    mcp_content_t *results = NULL;
    size_t result_count = 0;
    bool is_error = false;

    found->handler(name, arguments_json, &results, &result_count, &is_error, found->user_data);

    if (result_count > (SIZE_MAX - 4096) / 1024) {
        mcp_content_destroy(results, result_count);
        airy_err_push_ex(AIRY_ERR_OVERFLOW, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: result_count overflow");
        return AIRY_ERR_OVERFLOW;
    }
    size_t buf_size = 4096 + result_count * 1024;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        mcp_content_destroy(results, result_count);
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "{\"isError\":%s,\"content\":[",
                       is_error ? "true" : "false");

    for (size_t i = 0; i < result_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        const char *type_str = "text";
        switch (results[i].type) {
        case MCP_CONTENT_IMAGE:
            type_str = "image";
            break;
        case MCP_CONTENT_RESOURCE:
            type_str = "resource";
            break;
        case MCP_CONTENT_EMBEDDED:
            type_str = "embedded";
            break;
        default:
            break;
        }
        char *text_esc = json_string_escape(results[i].text);
        offset += snprintf(buf + offset, buf_size - offset, "{\"type\":\"%s\",\"text\":%s}",
                           type_str, text_esc);
        AIRY_FREE(text_esc);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;

    mcp_content_destroy(results, result_count);
    return 0;
}

int mcp_v1_handle_tools_call_streaming(mcp_v1_context_t *ctx, const char *name,
                                       const char *arguments_json, mcp_stream_callback_t callback,
                                       void *user_data)
{
    if (!ctx || !name || !callback) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_tools_call_streaming: failed");
        return AIRY_ERR_UNKNOWN;
    }

    mcp_stream_event_t start_event;
    mcp_stream_event_init(&start_event, MCP_STREAM_EVENT_PROGRESS, "{\"status\":\"started\"}");
    start_event.progress = 0;
    start_event.total = 100;
    callback(&start_event, user_data);
    AIRY_FREE(start_event.event_data);
    start_event.event_data = NULL;

    mcp_tool_entry_t *found = NULL;
    for (size_t i = 0; i < ctx->tool_count; i++) {
        if (strcmp(ctx->tools[i].tool.name, name) == 0) {
            found = &ctx->tools[i];
            break;
        }
    }

    if (!found) {
        char *name_esc = json_string_escape(name);
        char error_json[512];
        snprintf(
            error_json, sizeof(error_json),
            "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"Tool not found: %s\"}]}",
            name_esc);
        AIRY_FREE(name_esc);
        name_esc = NULL;
        emit_sse_event(callback, user_data, "tools/call/result", error_json);

        mcp_stream_event_t done_event;
        mcp_stream_event_init(&done_event, MCP_STREAM_EVENT_ERROR, "Tool not found");
        done_event.progress = 0;
        done_event.total = 100;
        callback(&done_event, user_data);
        AIRY_FREE(done_event.event_data);
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    mcp_content_t *results = NULL;
    size_t result_count = 0;
    bool is_error = false;

    found->handler(name, arguments_json, &results, &result_count, &is_error, found->user_data);

    for (size_t i = 0; i < result_count; i++) {
        size_t buf_size = 512 + (results[i].text ? strlen(results[i].text) : 0);
        char *chunk_json = AIRY_MALLOC(buf_size);
        if (chunk_json) {
            const char *type_str = "text";
            switch (results[i].type) {
            case MCP_CONTENT_IMAGE:
                type_str = "image";
                break;
            case MCP_CONTENT_RESOURCE:
                type_str = "resource";
                break;
            case MCP_CONTENT_EMBEDDED:
                type_str = "embedded";
                break;
            default:
                break;
            }
            snprintf(chunk_json, buf_size, "{\"index\":%zu,\"type\":\"%s\",\"partial\":true}", i,
                     type_str);

            size_t text_len = results[i].text ? strlen(results[i].text) : 0;
            size_t offset = 0;
            while (offset < text_len) {
                size_t chunk_size = 128;
                if (offset + chunk_size > text_len)
                    chunk_size = text_len - offset;

                char chunk_buf[256];
                __builtin_memcpy(chunk_buf, results[i].text + offset, chunk_size);
                chunk_buf[chunk_size] = '\0';

                char *escaped_chunk = json_string_escape(chunk_buf);
                char sse_data[1024];
                snprintf(sse_data, sizeof(sse_data), "%s{\"delta\":{\"text\":\"%s\"}}",
                         offset == 0 ? chunk_json : "", escaped_chunk);
                AIRY_FREE(escaped_chunk);

                emit_sse_event(callback, user_data, "tools/call/chunk", sse_data);
                offset += chunk_size;

                double pct = (double)offset / (double)(text_len > 0 ? text_len : 1) * 80.0;
                mcp_stream_event_t progress_evt;
                mcp_stream_event_init(&progress_evt, MCP_STREAM_EVENT_PROGRESS, NULL);
                progress_evt.progress = pct;
                progress_evt.total = 100;
                callback(&progress_evt, user_data);
                AIRY_FREE(progress_evt.event_data);
            }

            AIRY_FREE(chunk_json);
        }
    }

    size_t final_buf_size = 0;
    char *final_json = NULL;
    if (result_count <= (SIZE_MAX - 4096) / 1024) {
        final_buf_size = 4096 + result_count * 1024;
        final_json = AIRY_MALLOC(final_buf_size);
    }
    if (final_json) {
        size_t offset = 0;
        offset += snprintf(final_json + offset, final_buf_size - offset,
                           "{\"isError\":%s,\"content\":[", is_error ? "true" : "false");
        for (size_t i = 0; i < result_count; i++) {
            if (i > 0)
                offset += snprintf(final_json + offset, final_buf_size - offset, ",");
            const char *type_str = "text";
            switch (results[i].type) {
            case MCP_CONTENT_IMAGE:
                type_str = "image";
                break;
            case MCP_CONTENT_RESOURCE:
                type_str = "resource";
                break;
            case MCP_CONTENT_EMBEDDED:
                type_str = "embedded";
                break;
            default:
                break;
            }
            char *text_esc = json_string_escape(results[i].text);
            offset += snprintf(final_json + offset, final_buf_size - offset,
                               "{\"type\":\"%s\",\"text\":%s}", type_str, text_esc);
            AIRY_FREE(text_esc);
        }
        offset += snprintf(final_json + offset, final_buf_size - offset, "]}");

        emit_sse_event(callback, user_data, "tools/call/result", final_json);
        AIRY_FREE(final_json);
    }

    mcp_stream_event_t done_event;
    mcp_stream_event_init(&done_event, is_error ? MCP_STREAM_EVENT_ERROR : MCP_STREAM_EVENT_DONE,
                          is_error ? "completed with errors" : "completed successfully");
    done_event.progress = 100;
    done_event.total = 100;
    callback(&done_event, user_data);
    AIRY_FREE(done_event.event_data);

    mcp_content_destroy(results, result_count);
    return 0;
}
