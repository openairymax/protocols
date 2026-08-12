// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file langchain_adapter_tool.c
 * @brief LangChain adapter tool registration domain.
 *
 * Single responsibility: tool registration (langchain_register_tool) and
 * tool list query (langchain_list_tools).
 */

#define LOG_TAG "langchain_adapter"

#include "langchain_adapter.h"
#include "langchain_adapter_internal.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int langchain_register_tool(langchain_adapter_context_t *ctx, const langchain_tool_def_t *tool,
                            langchain_tool_executor_fn executor, void *user_data)
{
    if (!ctx || !tool || !tool->id)
        return AIRY_ERR_NULL_POINTER;
    if (ctx->tool_count >= LANGCHAIN_MAX_TOOLS)
        return AIRY_ERR_OVERFLOW;

    AIRY_MEMSET(&ctx->tools[ctx->tool_count], 0, sizeof(langchain_tool_def_t));
    ctx->tools[ctx->tool_count].id = AIRY_STRDUP(tool->id);
    ctx->tools[ctx->tool_count].name = tool->name ? AIRY_STRDUP(tool->name) : NULL;
    ctx->tools[ctx->tool_count].description =
        tool->description ? AIRY_STRDUP(tool->description) : NULL;
    ctx->tools[ctx->tool_count].function_schema_json =
        tool->function_schema_json ? AIRY_STRDUP(tool->function_schema_json) : NULL;
    ctx->tools[ctx->tool_count].tool_type = tool->tool_type;
    ctx->tools[ctx->tool_count].is_async = tool->is_async;

    ctx->tool_count++;
    return 0;
}

int langchain_list_tools(langchain_adapter_context_t *ctx, langchain_tool_def_t **tools,
                         size_t *count)
{
    if (!ctx || !tools || !count)
        return AIRY_ERR_NULL_POINTER;
    *tools = NULL;
    *count = 0;
    if (ctx->tool_count == 0)
        return 0;

    *tools = (langchain_tool_def_t *)AIRY_CALLOC(ctx->tool_count, sizeof(langchain_tool_def_t));
    if (!*tools)
        return AIRY_ERR_OUT_OF_MEMORY;

    for (size_t i = 0; i < ctx->tool_count; i++) {
        (*tools)[i].id = ctx->tools[i].id ? AIRY_STRDUP(ctx->tools[i].id) : NULL;
        (*tools)[i].name = ctx->tools[i].name ? AIRY_STRDUP(ctx->tools[i].name) : NULL;
        (*tools)[i].description =
            ctx->tools[i].description ? AIRY_STRDUP(ctx->tools[i].description) : NULL;
        (*tools)[i].function_schema_json = ctx->tools[i].function_schema_json ?
                                               AIRY_STRDUP(ctx->tools[i].function_schema_json) :
                                               NULL;
        (*tools)[i].tool_type = ctx->tools[i].tool_type;
        (*tools)[i].is_async = ctx->tools[i].is_async;
    }
    *count = ctx->tool_count;
    return 0;
}
