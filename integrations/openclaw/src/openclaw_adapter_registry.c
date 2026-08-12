// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_registry.c
 * @brief OpenClaw adapter registration and discovery domain (agent register/discover/unregister, tool register/list).
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

int openclaw_register_agent(openclaw_adapter_context_t *ctx, const openclaw_agent_card_t *card)
{
    if (!ctx || !card || !card->agent_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_register_agent: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->connected) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "Not connected to OpenClaw platform");
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    }

    for (size_t i = 0; i < ctx->registered_agent_count; i++) {
        if (strcmp(ctx->registered_agents[i].agent_id, card->agent_id) == 0) {
            __builtin_memcpy(&ctx->registered_agents[i], card, sizeof(openclaw_agent_card_t));
            if (card->agent_id)
                ctx->registered_agents[i].agent_id = AIRY_STRDUP(card->agent_id);
            if (card->name)
                ctx->registered_agents[i].name = AIRY_STRDUP(card->name);
            if (card->description)
                ctx->registered_agents[i].description = AIRY_STRDUP(card->description);
            if (card->version)
                ctx->registered_agents[i].version = AIRY_STRDUP(card->version);
            return 0;
        }
    }

    openclaw_agent_card_t *new_agents = (openclaw_agent_card_t *)AIRY_REALLOC(
        ctx->registered_agents, (ctx->registered_agent_count + 1) * sizeof(openclaw_agent_card_t));
    if (!new_agents)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    ctx->registered_agents = new_agents;
    AIRY_MEMSET(&ctx->registered_agents[ctx->registered_agent_count], 0,
                sizeof(openclaw_agent_card_t));

    openclaw_agent_card_t *target = &ctx->registered_agents[ctx->registered_agent_count];
    target->agent_id = card->agent_id ? AIRY_STRDUP(card->agent_id) : NULL;
    target->name = card->name ? AIRY_STRDUP(card->name) : NULL;
    target->description = card->description ? AIRY_STRDUP(card->description) : NULL;
    target->version = card->version ? AIRY_STRDUP(card->version) : NULL;
    target->supported_modalities = card->supported_modalities;
    target->security_level = card->security_level;
    target->max_concurrent_tasks = card->max_concurrent_tasks > 0 ? card->max_concurrent_tasks : 8;
    target->is_active = true;
    target->created_at = (uint64_t)(time(NULL));
    target->last_heartbeat = target->created_at;

    ctx->registered_agent_count++;
    return 0;
}

int openclaw_discover_agents(openclaw_adapter_context_t *ctx, const char *capability_filter,
                             openclaw_security_level_t min_level, openclaw_agent_card_t **agents,
                             size_t *count)
{
    if (!ctx || !agents || !count) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_discover_agents: failed");
        return AIRY_ERR_UNKNOWN;
    }
    *agents = NULL;
    *count = 0;

    if (!ctx->connected)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    size_t match_count = 0;
    for (size_t i = 0; i < ctx->registered_agent_count; i++) {
        const openclaw_agent_card_t *card = &ctx->registered_agents[i];
        if (card->is_active && card->security_level >= min_level) {
            match_count++;
        }
    }

    if (match_count == 0)
        return 0;

    *agents = (openclaw_agent_card_t *)AIRY_CALLOC(match_count, sizeof(openclaw_agent_card_t));
    if (!*agents)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    size_t idx = 0;
    for (size_t i = 0; i < ctx->registered_agent_count && idx < match_count; i++) {
        const openclaw_agent_card_t *card = &ctx->registered_agents[i];
        if (card->is_active && card->security_level >= min_level) {
            (*agents)[idx] = *card;
            (*agents)[idx].agent_id = card->agent_id ? AIRY_STRDUP(card->agent_id) : NULL;
            (*agents)[idx].name = card->name ? AIRY_STRDUP(card->name) : NULL;
            (*agents)[idx].description = card->description ? AIRY_STRDUP(card->description) : NULL;
            (*agents)[idx].version = card->version ? AIRY_STRDUP(card->version) : NULL;
            idx++;
        }
    }

    *count = idx;
    return 0;
}

int openclaw_unregister_agent(openclaw_adapter_context_t *ctx, const char *agent_id)
{
    if (!ctx || !agent_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_unregister_agent: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < ctx->registered_agent_count; i++) {
        if (strcmp(ctx->registered_agents[i].agent_id, agent_id) == 0) {
            openclaw_agent_card_destroy(&ctx->registered_agents[i]);
            if (i < ctx->registered_agent_count - 1) {
                __builtin_memmove(&ctx->registered_agents[i], &ctx->registered_agents[i + 1],
                                  (ctx->registered_agent_count - i - 1) *
                                      sizeof(openclaw_agent_card_t));
            }
            ctx->registered_agent_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int openclaw_register_tool(openclaw_adapter_context_t *ctx, const openclaw_tool_info_t *tool)
{
    if (!ctx || !tool || !tool->tool_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_register_tool: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->connected) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "Not connected");
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    }

    for (size_t i = 0; i < ctx->registered_tool_count; i++) {
        if (strcmp(ctx->registered_tools[i].tool_id, tool->tool_id) == 0) {
            openclaw_tool_info_destroy(&ctx->registered_tools[i]);
            ctx->registered_tools[i] = *tool;
            ctx->registered_tools[i].tool_id = tool->tool_id ? AIRY_STRDUP(tool->tool_id) : NULL;
            ctx->registered_tools[i].name = tool->name ? AIRY_STRDUP(tool->name) : NULL;
            ctx->registered_tools[i].description =
                tool->description ? AIRY_STRDUP(tool->description) : NULL;
            ctx->registered_tools[i].input_schema_json =
                tool->input_schema_json ? AIRY_STRDUP(tool->input_schema_json) : NULL;
            ctx->registered_tools[i].output_schema_json =
                tool->output_schema_json ? AIRY_STRDUP(tool->output_schema_json) : NULL;
            return 0;
        }
    }

    openclaw_tool_info_t *new_tools = (openclaw_tool_info_t *)AIRY_REALLOC(
        ctx->registered_tools, (ctx->registered_tool_count + 1) * sizeof(openclaw_tool_info_t));
    if (!new_tools)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    ctx->registered_tools = new_tools;
    AIRY_MEMSET(&ctx->registered_tools[ctx->registered_tool_count], 0,
                sizeof(openclaw_tool_info_t));

    openclaw_tool_info_t *target = &ctx->registered_tools[ctx->registered_tool_count];
    target->tool_id = tool->tool_id ? AIRY_STRDUP(tool->tool_id) : NULL;
    target->name = tool->name ? AIRY_STRDUP(tool->name) : NULL;
    target->description = tool->description ? AIRY_STRDUP(tool->description) : NULL;
    target->input_schema_json =
        tool->input_schema_json ? AIRY_STRDUP(tool->input_schema_json) : NULL;
    target->output_schema_json =
        tool->output_schema_json ? AIRY_STRDUP(tool->output_schema_json) : NULL;

    ctx->registered_tool_count++;
    return 0;
}

int openclaw_list_tools(openclaw_adapter_context_t *ctx, const char *agent_id,
                        openclaw_tool_info_t **tools, size_t *count)
{
    if (!ctx || !tools || !count) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_list_tools: failed");
        return AIRY_ERR_UNKNOWN;
    }
    *tools = NULL;
    *count = 0;

    if (!ctx->connected)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    size_t match_count = 0;
    for (size_t i = 0; i < ctx->registered_tool_count; i++) {
        if (!agent_id || (ctx->registered_tools[i].owner_agent_id &&
                          strcmp(ctx->registered_tools[i].owner_agent_id, agent_id) == 0)) {
            match_count++;
        }
    }

    if (match_count == 0)
        return 0;

    *tools = (openclaw_tool_info_t *)AIRY_CALLOC(match_count, sizeof(openclaw_tool_info_t));
    if (!*tools)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    size_t idx = 0;
    for (size_t i = 0; i < ctx->registered_tool_count && idx < match_count; i++) {
        if (!agent_id || (ctx->registered_tools[i].owner_agent_id &&
                          strcmp(ctx->registered_tools[i].owner_agent_id, agent_id) == 0)) {
            (*tools)[idx] = ctx->registered_tools[i];
            (*tools)[idx].tool_id = ctx->registered_tools[i].tool_id ?
                                        AIRY_STRDUP(ctx->registered_tools[i].tool_id) :
                                        NULL;
            (*tools)[idx].name =
                ctx->registered_tools[i].name ? AIRY_STRDUP(ctx->registered_tools[i].name) : NULL;
            (*tools)[idx].description = ctx->registered_tools[i].description ?
                                            AIRY_STRDUP(ctx->registered_tools[i].description) :
                                            NULL;
            idx++;
        }
    }

    *count = idx;
    return 0;
}
