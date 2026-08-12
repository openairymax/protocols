// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_stream.c
 * @brief MCP v1.0 streaming domain (stream config/stream event init/SSE line and event emission).
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

/* ========== Streaming Support Implementation (PROTO-001) ========== */
typedef struct {
    mcp_stream_config_t config;
    bool active;
} mcp_stream_state_t;

int mcp_v1_stream_config(mcp_v1_context_t *ctx, const mcp_stream_config_t *config)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_stream_config: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!config) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    ctx->stream_config = *config;
    return 0;
}

const char *mcp_stream_event_type_string(mcp_stream_event_type_t type)
{
    switch (type) {
    case MCP_STREAM_EVENT_CONTENT:
        return "content";
    case MCP_STREAM_EVENT_ERROR:
        return "error";
    case MCP_STREAM_EVENT_DONE:
        return "done";
    case MCP_STREAM_EVENT_PROGRESS:
        return "progress";
    case MCP_STREAM_EVENT_CANCELLED:
        return "cancelled";
    default:
        return "unknown";
    }
}

void mcp_stream_event_init(mcp_stream_event_t *event, mcp_stream_event_type_t type,
                           const char *data)
{
    if (!event)
        return;
    AIRY_MEMSET(event, 0, sizeof(*event));
    event->type = type;
    event->event_data = data ? AIRY_STRDUP(data) : NULL;
    event->data_size = data ? strlen(data) : 0;
}

void emit_sse_line(mcp_stream_callback_t callback, void *user_data, const char *field,
                   const char *value)
{
    if (!callback)
        return;

    size_t len = strlen(field) + (value ? strlen(value) : 0) + 16;
    char *sse_line = AIRY_MALLOC(len);
    if (sse_line) {
        snprintf(sse_line, len, "%s: %s\n", field, value ? value : "");
        mcp_stream_event_t event;
        mcp_stream_event_init(&event, MCP_STREAM_EVENT_CONTENT, sse_line);
        callback(&event, user_data);
        AIRY_FREE(sse_line);
    }
}

int emit_sse_event(mcp_stream_callback_t callback, void *user_data, const char *event_type,
                   const char *json_data)
{
    if (!callback)
        return 0;

    emit_sse_line(callback, user_data, "event", event_type);
    emit_sse_line(callback, user_data, "data", json_data);
    emit_sse_line(callback, user_data, "", NULL);

    return 0;
}
