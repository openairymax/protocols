// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the MCP v1.0 adapter split files.
 */

#ifndef MCP_V1_ADAPTER_INTERNAL_H
#define MCP_V1_ADAPTER_INTERNAL_H

#include "mcp_v1_adapter.h"
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

typedef struct {
    mcp_tool_t tool;
    mcp_tool_handler_t handler;
    void *user_data;
} mcp_tool_entry_t;

typedef struct {
    mcp_resource_t resource;
    mcp_resource_handler_t handler;
    void *user_data;
} mcp_resource_entry_t;

typedef struct {
    mcp_prompt_t prompt;
    mcp_prompt_handler_t handler;
    void *user_data;
} mcp_prompt_entry_t;

struct mcp_v1_context_s {
    mcp_v1_config_t config;

    mcp_tool_entry_t *tools;
    size_t tool_count;
    size_t tool_capacity;

    mcp_resource_entry_t *resources;
    size_t resource_count;
    size_t resource_capacity;

    mcp_resource_template_t *resource_templates;
    size_t template_count;
    size_t template_capacity;

    mcp_prompt_entry_t *prompts;
    size_t prompt_count;
    size_t prompt_capacity;

    mcp_sampling_handler_t sampling_handler;
    void *sampling_user_data;

    mcp_completion_handler_t completion_handler;
    void *completion_user_data;

    mcp_progress_callback_t progress_callback;
    void *progress_user_data;

    mcp_log_callback_t log_callback;
    void *log_user_data;

    mcp_log_level_t log_level;

    mcp_transport_t *transport;

    uint64_t request_counter;

    mcp_stream_config_t stream_config;
    int stream_active;
};

/* 列表响应缓冲区：open 申请并写入首段，close 补尾并移交所有权 */
typedef struct {
    char *buf;
    size_t size;
    size_t off;
} mcp_list_t;

static inline int mcp_list_open(mcp_list_t *l, size_t n, size_t per, const char *head)
{
    l->size = 4096 + n * per;
    l->buf = AIRY_MALLOC(l->size);
    l->off = 0;
    if (!l->buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }
    l->off += snprintf(l->buf, l->size, "%s", head);
    return 0;
}

static inline char *mcp_list_close(mcp_list_t *l)
{
    l->off += snprintf(l->buf + l->off, l->size - l->off, "]}");
    return l->buf;
}

/* Helpers shared across files (was static; now external linkage) */
char *json_string_escape(const char *str);
char *strdup_safe(const char *s);
void emit_sse_line(mcp_stream_callback_t callback, void *user_data, const char *field,
                   const char *value);
int emit_sse_event(mcp_stream_callback_t callback, void *user_data, const char *event_type,
                   const char *json_data);
const char *mcp_type_str(mcp_content_type_t type);
void mcp_v1_context_clear(mcp_v1_context_t *ctx);

#endif /* MCP_V1_ADAPTER_INTERNAL_H */
