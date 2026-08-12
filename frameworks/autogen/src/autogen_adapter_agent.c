// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file autogen_adapter_agent.c
 * @brief AutoGen adapter agent and group-chat domain.
 *
 * Single responsibility: agent instance create/destroy/list, group-chat
 * creation, tool registration and session queries.
 */

#define LOG_TAG "autogen_adapter"

#include "autogen_adapter.h"
#include "autogen_adapter_internal.h"

#include "airy_protocol_interface.h"
#include "error.h"
#include "airy_memory.h"
#include "types.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int autogen_create_agent(autogen_adapter_context_t *ctx, const autogen_agent_def_t *definition,
                         char *out_agent_id)
{
    if (!ctx || !definition || !out_agent_id)
        return AIRY_ERR_NULL_POINTER;

    static uint32_t agent_counter = 0;
    agent_counter++;

    snprintf(out_agent_id, 64, "ag-agent-%08x", agent_counter);

    autogen_agent_instance_t *agents =
        (autogen_agent_instance_t *)AIRY_REALLOC(ctx->agents, (ctx->agent_count + 1) *
                                                                  sizeof(autogen_agent_instance_t));
    if (!agents) {
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    ctx->agents = agents;

    AIRY_MEMSET(&ctx->agents[ctx->agent_count], 0, sizeof(autogen_agent_instance_t));
    ctx->agents[ctx->agent_count].agent_id = AIRY_STRDUP(out_agent_id);
    ctx->agents[ctx->agent_count].name =
        definition->name ? AIRY_STRDUP(definition->name) : out_agent_id;
    ctx->agents[ctx->agent_count].role = definition->role;
    ctx->agents[ctx->agent_count].is_active = true;
    ctx->agents[ctx->agent_count].messages_sent = 0;
    ctx->agents[ctx->agent_count].messages_received = 0;
    ctx->agents[ctx->agent_count].tool_calls_made = 0;

    ctx->agent_count++;
    return 0;
}

int autogen_destroy_agent(autogen_adapter_context_t *ctx, const char *agent_id)
{
    if (!ctx || !agent_id)
        return AIRY_ERR_NULL_POINTER;

    for (size_t i = 0; i < ctx->agent_count; i++) {
        if (strcmp(ctx->agents[i].agent_id, agent_id) == 0) {
            autogen_agent_instance_destroy(&ctx->agents[i]);
            if (i < ctx->agent_count - 1) {
                __builtin_memmove(&ctx->agents[i], &ctx->agents[i + 1],
                                  (ctx->agent_count - i - 1) * sizeof(autogen_agent_instance_t));
            }
            ctx->agent_count--;
            return 0;
        }
    }
    return AIRY_ERR_OUT_OF_MEMORY;
}

int autogen_list_agents(autogen_adapter_context_t *ctx, autogen_agent_instance_t **agents,
                        size_t *count)
{
    if (!ctx || !agents || !count)
        return AIRY_ERR_NULL_POINTER;
    *agents = NULL;
    *count = 0;
    if (ctx->agent_count == 0)
        return 0;

    *agents =
        (autogen_agent_instance_t *)AIRY_CALLOC(ctx->agent_count, sizeof(autogen_agent_instance_t));
    if (!*agents)
        return AIRY_ERR_OUT_OF_MEMORY;

    for (size_t i = 0; i < ctx->agent_count; i++) {
        (*agents)[i] = ctx->agents[i];
        (*agents)[i].agent_id =
            ctx->agents[i].agent_id ? AIRY_STRDUP(ctx->agents[i].agent_id) : NULL;
        (*agents)[i].name = ctx->agents[i].name ? AIRY_STRDUP(ctx->agents[i].name) : NULL;
    }
    *count = ctx->agent_count;
    return 0;
}

int autogen_create_group_chat(autogen_adapter_context_t *ctx,
                              const autogen_group_chat_def_t *definition, char *out_group_id)
{
    if (!ctx || !out_group_id)
        return AIRY_ERR_NULL_POINTER;

    static uint32_t gc_counter = 0;
    gc_counter++;
    snprintf(out_group_id, 64, "ag-group-%08x", gc_counter);

    autogen_group_chat_def_t *gcs = (autogen_group_chat_def_t *)AIRY_REALLOC(
        ctx->group_chats, (ctx->group_chat_count + 1) * sizeof(autogen_group_chat_def_t));
    if (!gcs)
        return AIRY_ERR_OUT_OF_MEMORY;
    ctx->group_chats = gcs;

    AIRY_MEMSET(&ctx->group_chats[ctx->group_chat_count], 0, sizeof(autogen_group_chat_def_t));
    ctx->group_chats[ctx->group_chat_count].id = AIRY_STRDUP(out_group_id);
    ctx->group_chats[ctx->group_chat_count].name =
        definition ?
            (definition->name ? AIRY_STRDUP(definition->name) : AIRY_STRDUP(out_group_id)) :
            AIRY_STRDUP(out_group_id);
    ctx->group_chats[ctx->group_chat_count].mode =
        definition ? definition->mode : GROUP_CHAT_ROUND_ROBIN;
    ctx->group_chats[ctx->group_chat_count].max_rounds = definition ? definition->max_rounds : 10;
    ctx->group_chats[ctx->group_chat_count].current_round = 0;
    ctx->group_chats[ctx->group_chat_count].allow_repeat_speaker = true;
    ctx->group_chats[ctx->group_chat_count].is_active = true;

    if (definition && definition->participant_ids && definition->participant_count > 0) {
        /* participant_ids is a char** (pointer); NULL after AIRY_MEMSET.
          * Allocate the pointer array before writing elements, or NULL[p] segfaults. */
        char **ids = (char **)AIRY_CALLOC(definition->participant_count, sizeof(char *));
        if (!ids)
            return AIRY_ERR_OUT_OF_MEMORY;
        for (size_t p = 0; p < definition->participant_count; p++) {
            ids[p] = AIRY_STRDUP(definition->participant_ids[p]);
            if (!ids[p]) {
                for (size_t q = 0; q < p; q++)
                    AIRY_FREE(ids[q]);
                AIRY_FREE(ids);
                return AIRY_ERR_OUT_OF_MEMORY;
            }
        }
        ctx->group_chats[ctx->group_chat_count].participant_ids = ids;
        ctx->group_chats[ctx->group_chat_count].participant_count = definition->participant_count;
    }

    ctx->group_chat_count++;
    return 0;
}

int autogen_register_tool(autogen_adapter_context_t *ctx, const char *name, const char *description,
                          const char *schema_json, autogen_tool_executor_fn executor,
                          void *user_data)
{
    if (!ctx || !name)
        return AIRY_ERR_NULL_POINTER;

    for (size_t i = 0; i < ctx->tool_count; i++) {
        if (strcmp(ctx->tool_names[i], name) == 0) {
            ctx->tool_executors[i] = executor;
            return 0;
        }
    }

    autogen_tool_executor_fn *new_exec = (autogen_tool_executor_fn *)
        AIRY_REALLOC(ctx->tool_executors, (ctx->tool_count + 1) * sizeof(autogen_tool_executor_fn));
    char **new_names =
        (char **)AIRY_REALLOC(ctx->tool_names, (ctx->tool_count + 1) * sizeof(char *));

    if (!new_exec || !new_names) {
        if (new_exec)
            AIRY_FREE(new_exec);
        if (new_names)
            AIRY_FREE(new_names);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    ctx->tool_executors = new_exec;
    ctx->tool_names = new_names;
    ctx->tool_names[ctx->tool_count] =
        AIRY_STRDUP(name ? name : (description ? description : "unnamed"));
    ctx->tool_executors[ctx->tool_count] = executor;
    ctx->tool_count++;

    return 0;
}

int autogen_get_conversation(autogen_adapter_context_t *ctx, const char *group_id,
                             autogen_conversation_t *conv)
{
    if (!ctx || !conv)
        return AIRY_ERR_NULL_POINTER;
    AIRY_MEMSET(conv, 0, sizeof(autogen_conversation_t));

    if (!group_id)
        return AIRY_ERR_INVALID_PARAM;

    for (size_t i = 0; i < ctx->conversation_count; i++) {
        if (ctx->conversations[i].conversation_id &&
            strcmp(ctx->conversations[i].conversation_id, group_id) == 0) {
            *conv = ctx->conversations[i];
            return 0;
        }
    }

    for (size_t i = 0; i < ctx->group_chat_count; i++) {
        if (ctx->group_chats[i].id && strcmp(ctx->group_chats[i].id, group_id) == 0) {
            conv->conversation_id = ctx->group_chats[i].id;
            conv->message_count = 0;
            conv->is_complete = false;
            return 0;
        }
    }

    return AIRY_ERR_NULL_POINTER;
}
