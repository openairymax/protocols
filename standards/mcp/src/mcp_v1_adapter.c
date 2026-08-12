// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_v1_adapter.c
 * @brief MCP v1.0 Protocol Adapter Implementation
 */

#include "mcp_v1_adapter.h"
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

char *json_string_escape(const char *str)
{
    if (!str)
        return AIRY_STRDUP("null");
    size_t len = strlen(str);
    size_t escaped_len = len * 2 + 3;
    char *escaped = AIRY_MALLOC(escaped_len);
    if (!escaped)
        return NULL;
    size_t j = 0;
    escaped[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
        case '"':
            escaped[j++] = '\\';
            escaped[j++] = '"';
            break;
        case '\\':
            escaped[j++] = '\\';
            escaped[j++] = '\\';
            break;
        case '\n':
            escaped[j++] = '\\';
            escaped[j++] = 'n';
            break;
        case '\r':
            escaped[j++] = '\\';
            escaped[j++] = 'r';
            break;
        case '\t':
            escaped[j++] = '\\';
            escaped[j++] = 't';
            break;
        default:
            escaped[j++] = str[i];
            break;
        }
    }
    escaped[j++] = '"';
    escaped[j] = '\0';
    return escaped;
}

char *strdup_safe(const char *s)
{
    return s ? AIRY_STRDUP(s) : NULL;
}

mcp_v1_config_t mcp_v1_config_default(void)
{
    mcp_v1_config_t config = {0};
    config.capabilities = MCP_CAP_TOOLS | MCP_CAP_RESOURCES | MCP_CAP_PROMPTS | MCP_CAP_LOGGING;
    config.default_timeout_ms = MCP_V1_DEFAULT_TIMEOUT_MS;
    config.max_tools = MCP_V1_MAX_TOOLS;
    config.max_resources = MCP_V1_MAX_RESOURCES;
    config.max_message_size = MCP_V1_MAX_MESSAGE_SIZE;
    config.enable_progress_notifications = true;
    config.enable_cancellation = true;
    config.enable_sampling = false;
    /* P0-01 fix: use string literals to avoid dynamic allocation.
     *
      * Historical issue: mcp_v1_config_default() returns mcp_v1_config_t by value;
      * callers (e.g. test_mcp_adapter.c) usually do not free server_name/server_version,
      * so AddressSanitizer reported a leak (186 bytes / 15 allocations).
     *
      * reported leaks. Fix: the default config points at literals (read-only, static storage).
      * This is safe because:
      *   1. 1. mcp_v1_context_create() copies strings into ctx via strdup_safe();
      *         ctx owns its dynamically allocated copies, freed by mcp_v1_context_destroy().
      *   2. 2. the default config itself needs no freeing (static literal storage).
      *   3. 3. AIRY_FREE(old_name) in mcp_adapter_connect() only affects ctx's copy,
      *         never the default config's literals.
     *
      * The (char*) cast is because server_name is char* (non-const) in the struct,
      * but the default config must not be modified; copy it via mcp_v1_context_create(). */
    config.server_name = (char *)"AgentRT MCP Server";
    config.server_version = (char *)MCP_V1_VERSION;
    return config;
}

mcp_v1_context_t *mcp_v1_context_create(const mcp_v1_config_t *config)
{

    if (!config)
        return NULL;

    mcp_v1_context_t *ctx = AIRY_CALLOC(1, sizeof(mcp_v1_context_t));
    if (!ctx) {
        LOG_ERROR("context allocation failed, size=%zu", sizeof(mcp_v1_context_t));
        return NULL;
    }

    ctx->config = *config;
    ctx->config.server_name = strdup_safe(config->server_name);
    ctx->config.server_version = strdup_safe(config->server_version);

    ctx->tool_capacity = 32;
    ctx->tools = AIRY_CALLOC(ctx->tool_capacity, sizeof(mcp_tool_entry_t));
    if (!ctx->tools) {
        LOG_ERROR("tools array allocation failed, capacity=%zu", ctx->tool_capacity);
        AIRY_FREE(ctx);
        return NULL;
    }

    ctx->resource_capacity = 16;
    ctx->resources = AIRY_CALLOC(ctx->resource_capacity, sizeof(mcp_resource_entry_t));
    if (!ctx->resources) {
        LOG_ERROR("resources array allocation failed, capacity=%zu", ctx->resource_capacity);
        AIRY_FREE(ctx->tools);
        AIRY_FREE(ctx);
        return NULL;
    }

    ctx->template_capacity = 16;
    ctx->resource_templates = AIRY_CALLOC(ctx->template_capacity, sizeof(mcp_resource_template_t));
    if (!ctx->resource_templates) {
        LOG_ERROR("resource_templates allocation failed, capacity=%zu", ctx->template_capacity);
        AIRY_FREE(ctx->resources);
        AIRY_FREE(ctx->tools);
        AIRY_FREE(ctx);
        return NULL;
    }

    ctx->prompt_capacity = 16;
    ctx->prompts = AIRY_CALLOC(ctx->prompt_capacity, sizeof(mcp_prompt_entry_t));
    if (!ctx->prompts) {
        LOG_ERROR("prompts array allocation failed, capacity=%zu", ctx->prompt_capacity);
        AIRY_FREE(ctx->resource_templates);
        AIRY_FREE(ctx->resources);
        AIRY_FREE(ctx->tools);
        AIRY_FREE(ctx);
        return NULL;
    }

    ctx->log_level = MCP_LOG_INFO;
    ctx->request_counter = 0;

    return ctx;
}

void mcp_v1_context_destroy(mcp_v1_context_t *ctx)
{
    if (!ctx)
        return;

    for (size_t i = 0; i < ctx->tool_count; i++) {
        AIRY_FREE(ctx->tools[i].tool.name);
        AIRY_FREE(ctx->tools[i].tool.description);
        AIRY_FREE(ctx->tools[i].tool.input_schema_json);
    }
    AIRY_FREE(ctx->tools);

    for (size_t i = 0; i < ctx->resource_count; i++) {
        AIRY_FREE(ctx->resources[i].resource.uri);
        AIRY_FREE(ctx->resources[i].resource.name);
        AIRY_FREE(ctx->resources[i].resource.description);
        AIRY_FREE(ctx->resources[i].resource.mime_type);
    }
    AIRY_FREE(ctx->resources);

    for (size_t i = 0; i < ctx->template_count; i++) {
        AIRY_FREE(ctx->resource_templates[i].uri_template);
        AIRY_FREE(ctx->resource_templates[i].name);
        AIRY_FREE(ctx->resource_templates[i].description);
        AIRY_FREE(ctx->resource_templates[i].mime_type);
    }
    AIRY_FREE(ctx->resource_templates);

    for (size_t i = 0; i < ctx->prompt_count; i++) {
        AIRY_FREE(ctx->prompts[i].prompt.name);
        AIRY_FREE(ctx->prompts[i].prompt.description);
        AIRY_FREE(ctx->prompts[i].prompt.arguments_schema_json);
    }
    AIRY_FREE(ctx->prompts);

    AIRY_FREE(ctx->config.server_name);
    AIRY_FREE(ctx->config.server_version);
    AIRY_FREE(ctx);
}

int mcp_v1_register_tool(mcp_v1_context_t *ctx, const mcp_tool_t *tool, mcp_tool_handler_t handler,
                         void *user_data)
{
    /* As with register_resource/register_prompt: handler may be NULL (late binding).
      * Only error if handler is still NULL at call time; registration need not have it ready. */
    if (!ctx || !tool) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_register_tool: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->tool_count >= ctx->config.max_tools) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    if (ctx->tool_count >= ctx->tool_capacity) {
        size_t new_cap = ctx->tool_capacity * 2;
        mcp_tool_entry_t *new_tools = AIRY_REALLOC(ctx->tools, new_cap * sizeof(mcp_tool_entry_t));
        if (!new_tools) {
            airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                             "mcp_v1_adapter: null pointer");
            return AIRY_ERR_NULL_POINTER;
        }
        ctx->tools = new_tools;
        ctx->tool_capacity = new_cap;
    }

    mcp_tool_entry_t *entry = &ctx->tools[ctx->tool_count];
    entry->tool.name = strdup_safe(tool->name);
    entry->tool.description = strdup_safe(tool->description);
    entry->tool.input_schema_json = strdup_safe(tool->input_schema_json);
    entry->tool.required_caps = tool->required_caps;
    entry->handler = handler;
    entry->user_data = user_data;
    ctx->tool_count++;

    return 0;
}

int mcp_v1_register_resource(mcp_v1_context_t *ctx, const mcp_resource_t *resource,
                             mcp_resource_handler_t handler, void *user_data)
{
    if (!ctx || !resource) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_register_resource: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->resource_count >= ctx->config.max_resources) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    if (ctx->resource_count >= ctx->resource_capacity) {
        size_t new_cap = ctx->resource_capacity * 2;
        mcp_resource_entry_t *new_res =
            AIRY_REALLOC(ctx->resources, new_cap * sizeof(mcp_resource_entry_t));
        if (!new_res) {
            airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                             "mcp_v1_adapter: null pointer");
            return AIRY_ERR_NULL_POINTER;
        }
        ctx->resources = new_res;
        ctx->resource_capacity = new_cap;
    }

    mcp_resource_entry_t *entry = &ctx->resources[ctx->resource_count];
    entry->resource.uri = strdup_safe(resource->uri);
    entry->resource.name = strdup_safe(resource->name);
    entry->resource.description = strdup_safe(resource->description);
    entry->resource.mime_type = strdup_safe(resource->mime_type);
    entry->handler = handler;
    entry->user_data = user_data;
    ctx->resource_count++;

    return 0;
}

int mcp_v1_register_resource_template(mcp_v1_context_t *ctx,
                                      const mcp_resource_template_t *template)
{
    if (!ctx || !template) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_register_resource_template: failed");
        return AIRY_ERR_UNKNOWN;
    }

    if (ctx->template_count >= ctx->template_capacity) {
        size_t new_cap = ctx->template_capacity * 2;
        mcp_resource_template_t *new_tpl =
            AIRY_REALLOC(ctx->resource_templates, new_cap * sizeof(mcp_resource_template_t));
        if (!new_tpl) {
            airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                             "mcp_v1_adapter: null pointer");
            return AIRY_ERR_NULL_POINTER;
        }
        ctx->resource_templates = new_tpl;
        ctx->template_capacity = new_cap;
    }

    mcp_resource_template_t *entry = &ctx->resource_templates[ctx->template_count];
    entry->uri_template = strdup_safe(template->uri_template);
    entry->name = strdup_safe(template->name);
    entry->description = strdup_safe(template->description);
    entry->mime_type = strdup_safe(template->mime_type);
    ctx->template_count++;

    return 0;
}

int mcp_v1_register_prompt(mcp_v1_context_t *ctx, const mcp_prompt_t *prompt,
                           mcp_prompt_handler_t handler, void *user_data)
{
    if (!ctx || !prompt) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_register_prompt: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->prompt_count >= MCP_V1_MAX_PROMPTS) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    if (ctx->prompt_count >= ctx->prompt_capacity) {
        size_t new_cap = ctx->prompt_capacity * 2;
        mcp_prompt_entry_t *new_prompts =
            AIRY_REALLOC(ctx->prompts, new_cap * sizeof(mcp_prompt_entry_t));
        if (!new_prompts) {
            airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                             "mcp_v1_adapter: null pointer");
            return AIRY_ERR_NULL_POINTER;
        }
        ctx->prompts = new_prompts;
        ctx->prompt_capacity = new_cap;
    }

    mcp_prompt_entry_t *entry = &ctx->prompts[ctx->prompt_count];
    entry->prompt.name = strdup_safe(prompt->name);
    entry->prompt.description = strdup_safe(prompt->description);
    entry->prompt.arguments_schema_json = strdup_safe(prompt->arguments_schema_json);
    entry->handler = handler;
    entry->user_data = user_data;
    ctx->prompt_count++;

    return 0;
}

int mcp_v1_set_sampling_handler(mcp_v1_context_t *ctx, mcp_sampling_handler_t handler,
                                void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_sampling_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->sampling_handler = handler;
    ctx->sampling_user_data = user_data;
    if (handler)
        ctx->config.capabilities |= MCP_CAP_SAMPLING;
    return 0;
}

int mcp_v1_set_completion_handler(mcp_v1_context_t *ctx, mcp_completion_handler_t handler,
                                  void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_completion_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->completion_handler = handler;
    ctx->completion_user_data = user_data;
    if (handler)
        ctx->config.capabilities |= MCP_CAP_COMPLETION;
    return 0;
}

int mcp_v1_set_progress_callback(mcp_v1_context_t *ctx, mcp_progress_callback_t callback,
                                 void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_progress_callback: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->progress_callback = callback;
    ctx->progress_user_data = user_data;
    return 0;
}

int mcp_v1_set_log_callback(mcp_v1_context_t *ctx, mcp_log_callback_t callback, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_log_callback: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->log_callback = callback;
    ctx->log_user_data = user_data;
    return 0;
}

int mcp_v1_set_log_level(mcp_v1_context_t *ctx, mcp_log_level_t level)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_log_level: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->log_level = level;
    return 0;
}

int mcp_v1_set_transport(mcp_v1_context_t *ctx, mcp_transport_t *transport)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_set_transport: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->transport = transport;
    return 0;
}

mcp_transport_t *mcp_v1_get_transport(mcp_v1_context_t *ctx)
{
    if (!ctx)
        return NULL;
    return ctx->transport;
}

size_t mcp_v1_get_tool_count(mcp_v1_context_t *ctx)
{
    return ctx ? ctx->tool_count : 0;
}

size_t mcp_v1_get_resource_count(mcp_v1_context_t *ctx)
{
    return ctx ? ctx->resource_count : 0;
}

size_t mcp_v1_get_prompt_count(mcp_v1_context_t *ctx)
{
    return ctx ? ctx->prompt_count : 0;
}

uint32_t mcp_v1_get_capabilities(mcp_v1_context_t *ctx)
{
    return ctx ? ctx->config.capabilities : 0;
}

void mcp_content_destroy(mcp_content_t *content, size_t count)
{
    if (!content)
        return;
    for (size_t i = 0; i < count; i++) {
        AIRY_FREE(content[i].text);
        AIRY_FREE(content[i].mime_type);
        AIRY_FREE(content[i].uri);
        AIRY_FREE(content[i].data);
    }
    AIRY_FREE(content);
}

void mcp_sampling_result_destroy(mcp_sampling_result_t *result)
{
    if (!result)
        return;
    AIRY_FREE(result->model);
    AIRY_FREE(result->stop_reason);
    mcp_content_destroy(result->content, result->content_count);
}

void mcp_completion_result_destroy(mcp_completion_result_t *result)
{
    if (!result)
        return;
    for (size_t i = 0; i < result->value_count; i++) {
        AIRY_FREE(result->values[i]);
    }
    AIRY_FREE(result->values);
}
