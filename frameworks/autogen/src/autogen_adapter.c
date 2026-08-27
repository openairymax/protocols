// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file autogen_adapter.c
 * @brief AutoGen framework adapter implementation.
 *
 * This file keeps the adapter lifecycle (create/destroy/config defaults/
 * statistics/callback registration), common resource release and the protocol
 * adapter registry (autogen_get_protocol_adapter). Agents and group chats,
 * message codec and protocol callbacks live in autogen_adapter_agent.c /
 * autogen_adapter_msg.c / autogen_adapter_proto.c.
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

autogen_config_t autogen_config_default(void)
{
    autogen_config_t cfg = {0};
    cfg.base_url = "http://127.0.0.1:8080";
    cfg.api_key = NULL;
    cfg.timeout_ms = AUTOGEN_DEFAULT_TIMEOUT_MS;
    cfg.enable_code_execution = true;
    cfg.enable_human_loop = false;
    cfg.enable_streaming = true;
    cfg.max_agents_per_group = 8;
    cfg.max_history_per_conv = 1000;
    cfg.max_code_execution_sec = 60;
    cfg.default_llm_model = "gpt-4o";
    cfg.work_dir = NULL;
    cfg.cache_dir = NULL;
    return cfg;
}

autogen_adapter_context_t *autogen_adapter_create(const autogen_config_t *config)
{
    if (!config)
        return NULL;

    autogen_adapter_context_t *ctx =
        (autogen_adapter_context_t *)AIRY_CALLOC(1, sizeof(autogen_adapter_context_t));
    if (!ctx)
        return NULL;

    __builtin_memcpy(&ctx->config, config, sizeof(autogen_config_t));
    if (config->base_url)
        ctx->config.base_url = AIRY_STRDUP(config->base_url);
    if (config->api_key)
        ctx->config.api_key = AIRY_STRDUP(config->api_key);
    if (config->default_llm_model)
        ctx->config.default_llm_model = AIRY_STRDUP(config->default_llm_model);
    if (config->work_dir)
        ctx->config.work_dir = AIRY_STRDUP(config->work_dir);
    if (config->cache_dir)
        ctx->config.cache_dir = AIRY_STRDUP(config->cache_dir);

    ctx->initialized = true;
    ctx->agents = NULL;
    ctx->agent_count = 0;
    ctx->group_chats = NULL;
    ctx->group_chat_count = 0;
    ctx->conversations = NULL;
    ctx->conversation_count = 0;
    ctx->total_chats_initiated = 0;
    ctx->total_messages_exchanged = 0;

    return ctx;
}

void autogen_adapter_destroy(autogen_adapter_context_t *ctx)
{
    if (!ctx)
        return;

    AIRY_FREE(ctx->config.base_url);
    AIRY_FREE(ctx->config.api_key);
    AIRY_FREE(ctx->config.default_llm_model);
    AIRY_FREE(ctx->config.work_dir);
    AIRY_FREE(ctx->config.cache_dir);

    for (size_t i = 0; i < ctx->agent_count; i++)
        autogen_agent_instance_destroy(&ctx->agents[i]);
    AIRY_FREE(ctx->agents);

    for (size_t i = 0; i < ctx->group_chat_count; i++)
        autogen_group_chat_def_destroy(&ctx->group_chats[i]);
    AIRY_FREE(ctx->group_chats);

    for (size_t i = 0; i < ctx->conversation_count; i++)
        autogen_conversation_destroy(&ctx->conversations[i]);
    AIRY_FREE(ctx->conversations);

    /* P0-03 fix: free tool resources.
     *
      * Historical issue: autogen_register_tool() grew tool_names/tool_executors via
      * AIRY_REALLOC and strdup'd names, but autogen_adapter_destroy() leaked them;
      * ASAN found 8B(tool_executors) + 8B(tool_names) + 11B("web_search" str) leaks.
     *
      * Fix: loop-free tool_names[i] in destroy, then free both arrays. */
    for (size_t i = 0; i < ctx->tool_count; i++)
        AIRY_FREE(ctx->tool_names[i]);
    AIRY_FREE(ctx->tool_names);
    AIRY_FREE(ctx->tool_executors);

    AIRY_FREE(ctx->connected_endpoint);
    AIRY_FREE(ctx->send_buffer);

    AIRY_MEMSET(ctx, 0, sizeof(autogen_adapter_context_t));
    AIRY_FREE(ctx);
}

bool autogen_adapter_is_initialized(const autogen_adapter_context_t *ctx)
{
    return ctx && ctx->initialized;
}

const char *autogen_adapter_version(void)
{
    return AUTOGEN_ADAPTER_VERSION;
}

int autogen_set_code_executor(autogen_adapter_context_t *ctx, autogen_code_executor_fn executor,
                              void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->code_executor = executor;
    ctx->code_executor_data = user_data;
    return 0;
}

int autogen_set_human_callback(autogen_adapter_context_t *ctx, autogen_human_callback_fn callback,
                               void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->human_callback = callback;
    ctx->human_callback_data = user_data;
    return 0;
}

int autogen_set_message_hook(autogen_adapter_context_t *ctx, autogen_message_hook_fn hook,
                             void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->message_hook = hook;
    ctx->message_hook_data = user_data;
    return 0;
}

int autogen_set_llm_callback(autogen_adapter_context_t *ctx, autogen_llm_callback_fn callback,
                             void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->llm_callback = callback;
    ctx->llm_callback_data = user_data;
    return 0;
}

int autogen_get_statistics(autogen_adapter_context_t *ctx, char *stats_json, size_t buffer_size)
{
    if (!ctx || !stats_json || buffer_size < 64)
        return AIRY_ERR_NULL_POINTER;

    int written =
        snprintf(stats_json, buffer_size,
                 "{"
                 "\"adapter_version\":\"%s\","
                 "\"total_agents\":%zu,"
                 "\"active_groups\":%zu,"
                 "\"chats_initiated\":%llu,"
                 "\"messages_exchanged\":%llu,"
                 "\"conversations\":%zu,"
                 "\"code_execution\":%s,"
                 "\"human_loop\":%s"
                 "}",
                 AUTOGEN_ADAPTER_VERSION, ctx->agent_count, ctx->group_chat_count,
                 (unsigned long long)ctx->total_chats_initiated,
                 (unsigned long long)ctx->total_messages_exchanged, ctx->conversation_count,
                 ctx->config.enable_code_execution ? "true" : "false",
                 ctx->config.enable_human_loop ? "true" : "false");

    return (written >= 0 && (size_t)written < buffer_size) ? 0 : -2;
}

static int autogen_proto_init(void *context)
{
    autogen_config_t config = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&config);
    if (!ctx)
        return AIRY_ERR_OUT_OF_MEMORY;
    *(void **)context = ctx;
    return 0;
}

static int autogen_proto_destroy(void *context)
{
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    if (ctx) {
        autogen_adapter_destroy(ctx);
    }
    return 0;
}

__attribute__((unused)) static int autogen_adapter_deinit(void *context)
{
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    autogen_adapter_destroy(ctx);
    return 0;
}

const proto_adapter_t *autogen_get_protocol_adapter(void)
{
    static proto_adapter_t adapter = {0};
    static bool initialized = false;

    if (!initialized) {
        adapter.name = "AutoGen";
        adapter.version = AUTOGEN_ADAPTER_VERSION;
        adapter.description = "Microsoft AutoGen Framework Adapter - multi-agent conversations, "
                              "group chat, code execution, human-in-the-loop";
        /* P0-15: PROTOCOL_CUSTOM → AIRY_PROTOCOL_A2A。
          * AutoGen is multi-agent collaboration; semantically closest to A2A. */
        adapter.type = AIRY_PROTOCOL_A2A;
        adapter.init = autogen_proto_init;
        adapter.destroy = autogen_proto_destroy;
        adapter.encode = autogen_proto_encode;
        adapter.decode = autogen_proto_decode;
        adapter.connect = autogen_proto_connect;
        adapter.disconnect = autogen_proto_disconnect;
        adapter.is_connected = autogen_proto_is_connected;
        adapter.send = autogen_proto_send;
        adapter.receive = autogen_proto_receive;
        adapter.handle_request = autogen_proto_handle_request;
        adapter.get_version = autogen_proto_get_version;
        adapter.capabilities = autogen_proto_capabilities;
        adapter.get_stats = autogen_proto_get_stats;
        initialized = true;
    }
    return &adapter;
}

void autogen_agent_def_destroy(autogen_agent_def_t *def)
{
    if (!def)
        return;
    AIRY_FREE(def->id);
    AIRY_FREE(def->name);
    AIRY_FREE(def->system_message);
    AIRY_FREE(def->llm_config_json);
    for (size_t i = 0; i < def->transition_count; i++)
        AIRY_FREE(def->allowed_transitions[i]);
    AIRY_FREE(def->allowed_transitions);
    AIRY_MEMSET(def, 0, sizeof(autogen_agent_def_t));
}

void autogen_agent_instance_destroy(autogen_agent_instance_t *inst)
{
    if (!inst)
        return;
    AIRY_FREE(inst->agent_id);
    AIRY_FREE(inst->name);
    AIRY_MEMSET(inst, 0, sizeof(autogen_agent_instance_t));
}

void autogen_group_chat_def_destroy(autogen_group_chat_def_t *gc)
{
    if (!gc)
        return;
    AIRY_FREE(gc->id);
    AIRY_FREE(gc->name);
    AIRY_FREE(gc->speaker_selection_prompt);
    for (size_t i = 0; i < gc->participant_count; i++)
        AIRY_FREE(gc->participant_ids[i]);
    AIRY_FREE(gc->participant_ids);
    AIRY_MEMSET(gc, 0, sizeof(autogen_group_chat_def_t));
}

void autogen_message_destroy(autogen_message_t *msg)
{
    if (!msg)
        return;
    AIRY_FREE(msg->message_id);
    AIRY_FREE(msg->sender_id);
    AIRY_FREE(msg->receiver_id);
    AIRY_FREE(msg->content);
    AIRY_FREE(msg->metadata_json);
    for (size_t i = 0; i < msg->tool_call_count; i++)
        AIRY_FREE(msg->tool_calls[i]);
    AIRY_FREE(msg->tool_calls);
    AIRY_MEMSET(msg, 0, sizeof(autogen_message_t));
}

void autogen_conversation_destroy(autogen_conversation_t *conv)
{
    if (!conv)
        return;
    AIRY_FREE(conv->conversation_id);
    AIRY_FREE(conv->summary);
    AIRY_FREE(conv->termination_reason);
    for (size_t i = 0; i < conv->message_count; i++)
        autogen_message_destroy(&conv->messages[i]);
    AIRY_FREE(conv->messages);
    AIRY_MEMSET(conv, 0, sizeof(autogen_conversation_t));
}

void autogen_group_chat_result_destroy(autogen_group_chat_result_t *result)
{
    if (!result)
        return;
    AIRY_FREE(result->group_id);
    AIRY_FREE(result->initiator_id);
    AIRY_FREE(result->initial_message);
    if (result->conversation) {
        autogen_conversation_destroy(result->conversation);
        AIRY_FREE(result->conversation);
    }
    AIRY_FREE(result->final_summary);
    AIRY_FREE(result->error_message);
    AIRY_MEMSET(result, 0, sizeof(autogen_group_chat_result_t));
}
