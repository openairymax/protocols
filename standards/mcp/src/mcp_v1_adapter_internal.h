// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the MCP v1.0 adapter split files.
 */

#ifndef MCP_V1_ADAPTER_INTERNAL_H
#define MCP_V1_ADAPTER_INTERNAL_H

#include "mcp_v1_adapter.h"

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

/* Helpers shared across files (was static; now external linkage) */
char *json_string_escape(const char *str);
char *strdup_safe(const char *s);
void emit_sse_line(mcp_stream_callback_t callback, void *user_data, const char *field,
                   const char *value);
int emit_sse_event(mcp_stream_callback_t callback, void *user_data, const char *event_type,
                   const char *json_data);

#endif /* MCP_V1_ADAPTER_INTERNAL_H */
