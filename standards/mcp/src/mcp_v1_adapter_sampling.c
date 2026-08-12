// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_sampling.c
 * @brief MCP v1.0 sampling and event domain (sampling/completion/progress/cancel and streaming variants).
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

int mcp_v1_handle_sampling(mcp_v1_context_t *ctx, const mcp_sampling_params_t *params,
                           mcp_sampling_result_t *result)
{
    if (!ctx || !params || !result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_sampling: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->sampling_handler) {
        LOG_WARN("sampling handler not registered, cannot handle sampling request");
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (!(ctx->config.capabilities & MCP_CAP_SAMPLING)) {
        LOG_WARN("sampling capability not enabled, caps=0x%x", ctx->config.capabilities);
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    ctx->sampling_handler(params, result, ctx->sampling_user_data);
    return 0;
}

int mcp_v1_handle_completion(mcp_v1_context_t *ctx, const mcp_completion_request_t *request,
                             mcp_completion_result_t *result)
{
    if (!ctx || !request || !result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_completion: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->completion_handler) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    ctx->completion_handler(request, result, ctx->completion_user_data);
    return 0;
}

int mcp_v1_send_progress(mcp_v1_context_t *ctx, const char *progress_token, double progress,
                         double total)
{
    if (!ctx || !progress_token) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_send_progress: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->progress_callback) {
        ctx->progress_callback(progress_token, progress, total, ctx->progress_user_data);
    }
    return 0;
}

int mcp_v1_notify_cancelled(mcp_v1_context_t *ctx, const char *request_id, const char *reason)
{
    if (!ctx || !request_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_notify_cancelled: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->log_callback) {
        ctx->log_callback(MCP_LOG_INFO, "mcp", reason ? reason : "Request cancelled",
                          ctx->log_user_data);
    }
    return 0;
}

int mcp_v1_handle_sampling_streaming(mcp_v1_context_t *ctx, const mcp_sampling_params_t *params,
                                     mcp_stream_callback_t callback, void *user_data)
{
    if (!ctx || !params || !callback) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_sampling_streaming: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->sampling_handler) {
        LOG_WARN("sampling handler not registered for streaming request");
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    if (!(ctx->config.capabilities & MCP_CAP_SAMPLING)) {
        LOG_WARN("sampling capability not enabled for streaming, caps=0x%x",
                 ctx->config.capabilities);
        return AIRY_ERR_NULL_POINTER;
    }

    mcp_stream_event_t start_event;
    mcp_stream_event_init(&start_event, MCP_STREAM_EVENT_PROGRESS,
                          "{\"status\":\"sampling_started\"}");
    start_event.progress = 0;
    start_event.total = 100;
    callback(&start_event, user_data);
    AIRY_FREE(start_event.event_data);
    start_event.event_data = NULL;

    mcp_sampling_result_t result;
    AIRY_MEMSET(&result, 0, sizeof(result));

    ctx->sampling_handler(params, &result, ctx->sampling_user_data);

    if (result.content && result.content_count > 0) {
        for (size_t i = 0; i < result.content_count; i++) {
            if (result.content[i].text && result.content[i].text[0]) {
                size_t text_len = strlen(result.content[i].text);
                size_t offset = 0;
                while (offset < text_len) {
                    size_t chunk_size = 128;
                    if (offset + chunk_size > text_len)
                        chunk_size = text_len - offset;

                    char chunk_buf[256];
                    __builtin_memcpy(chunk_buf, result.content[i].text + offset, chunk_size);
                    chunk_buf[chunk_size] = '\0';

                    char *escaped_chunk = json_string_escape(chunk_buf);
                    char *model_esc = json_string_escape(result.model ? result.model : "unknown");
                    char sse_data[512];
                    snprintf(sse_data, sizeof(sse_data),
                             "{\"model\":%s,\"delta\":{\"type\":\"text\",\"text\":\"%s\"},"
                             "\"index\":%zu}",
                             model_esc, escaped_chunk, i);
                    AIRY_FREE(escaped_chunk);
                    AIRY_FREE(model_esc);
                    emit_sse_event(callback, user_data, "sampling/chunk", sse_data);

                    offset += chunk_size;

                    double pct = (double)offset / (double)(text_len > 0 ? text_len : 1) * 90.0;
                    mcp_stream_event_t progress_evt;
                    mcp_stream_event_init(&progress_evt, MCP_STREAM_EVENT_PROGRESS, NULL);
                    progress_evt.progress = pct;
                    progress_evt.total = 100;
                    callback(&progress_evt, user_data);
                    AIRY_FREE(progress_evt.event_data);
                }
            }
        }
    }

    char *model_esc = json_string_escape(result.model ? result.model : "unknown");
    char *stop_esc = json_string_escape(result.stop_reason ? result.stop_reason : "end_turn");
    char final_result[2048];
    snprintf(final_result, sizeof(final_result),
             "{\"model\":%s,\"stopReason\":%s,\"stoppedEarly\":%s}", model_esc, stop_esc,
             result.stopped_early ? "true" : "false");
    AIRY_FREE(model_esc);
    model_esc = NULL;
    AIRY_FREE(stop_esc);
    stop_esc = NULL;
    emit_sse_event(callback, user_data, "sampling/result", final_result);

    mcp_stream_event_t done_event;
    mcp_stream_event_init(&done_event, MCP_STREAM_EVENT_DONE, "Sampling complete");
    done_event.progress = 100;
    done_event.total = 100;
    callback(&done_event, user_data);
    AIRY_FREE(done_event.event_data);

    mcp_sampling_result_destroy(&result);
    return 0;
}
