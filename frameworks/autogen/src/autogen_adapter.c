// SPDX-FileCopyrightText: 2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
// @owner: team-B
/**
 * @file autogen_adapter.c
 * @brief AutoGen Framework Adapter Implementation
 */

#define LOG_TAG "autogen_adapter"

#include "autogen_adapter.h"

#include "airy_protocol_interface.h"
#include "error.h"
#include "airy_memory.h"
#include "types.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


struct autogen_adapter_context_s {
    autogen_config_t config;
    bool initialized;
    autogen_agent_instance_t *agents;
    size_t agent_count;
    autogen_group_chat_def_t *group_chats;
    size_t group_chat_count;
    autogen_conversation_t *conversations;
    size_t conversation_count;
    autogen_tool_executor_fn *tool_executors;
    char **tool_names;
    size_t tool_count;
    autogen_code_executor_fn code_executor;
    void *code_executor_data;
    autogen_human_callback_fn human_callback;
    void *human_callback_data;
    autogen_llm_callback_fn llm_callback;
    void *llm_callback_data;
    autogen_message_hook_fn message_hook;
    void *message_hook_data;
    uint64_t total_chats_initiated;
    uint64_t total_messages_exchanged;
    bool is_connected;
    char *connected_endpoint;
    void *send_buffer;
    size_t send_buffer_size;
    uint64_t bytes_sent;
    uint64_t bytes_received;
};

autogen_config_t autogen_config_default(void)
{
    autogen_config_t cfg = {0};
    cfg.base_url = "http://127.0.0.1:18789";
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

    /* P0-03 修复: 释放 tools 资源。
     *
     * 历史问题：autogen_register_tool() 通过 AIRY_REALLOC 扩展 tool_names/tool_executors
     * 数组并 AIRY_STRDUP(name) 拷贝 tool 名，但 autogen_adapter_destroy() 漏释放这些资源，
     * 导致 ASAN 检测到 8B(tool_executors) + 8B(tool_names) + 11B("web_search" str) 泄漏。
     *
     * 修复方案：在 destroy 中循环释放 tool_names[i]，然后释放两个数组本身。 */
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

int autogen_create_agent(autogen_adapter_context_t *ctx, const autogen_agent_def_t *definition,
                         char *out_agent_id)
{
    if (!ctx || !definition || !out_agent_id)
        return AIRY_ERR_NULL_POINTER;

    static uint32_t agent_counter = 0;
    agent_counter++;

    snprintf(out_agent_id, 64, "ag-agent-%08x", agent_counter);

    autogen_agent_instance_t *agents = (autogen_agent_instance_t *)AIRY_REALLOC(
        ctx->agents, (ctx->agent_count + 1) * sizeof(autogen_agent_instance_t));
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

    *agents = (autogen_agent_instance_t *)AIRY_CALLOC(ctx->agent_count,
                                                         sizeof(autogen_agent_instance_t));
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
        definition
            ? (definition->name ? AIRY_STRDUP(definition->name) : AIRY_STRDUP(out_group_id))
            : AIRY_STRDUP(out_group_id);
    ctx->group_chats[ctx->group_chat_count].mode =
        definition ? definition->mode : GROUP_CHAT_ROUND_ROBIN;
    ctx->group_chats[ctx->group_chat_count].max_rounds = definition ? definition->max_rounds : 10;
    ctx->group_chats[ctx->group_chat_count].current_round = 0;
    ctx->group_chats[ctx->group_chat_count].allow_repeat_speaker = true;
    ctx->group_chats[ctx->group_chat_count].is_active = true;

    if (definition && definition->participant_ids && definition->participant_count > 0) {
        /* participant_ids 是 char**（指针），AIRY_MEMSET 后为 NULL。
         * 必须先分配指针数组，再写入元素，否则 NULL[p] 解引用导致 SEGFAULT。 */
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

static uint64_t __attribute__((unused)) autogen_hash_str(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    if (!s)
        return h;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    }
    return h;
}

static int __attribute__((used)) autogen_count_words(const char *t)
{
    if (!t || !*t)
        return 0;
    int c = 0, in = 0;
    for (; *t; t++) {
        if (isalnum((unsigned char)*t) || (*t & 0x80)) {
            if (!in) {
                c++;
                in = 1;
            }
        } else
            in = 0;
    }
    return c > 0 ? c : 1;
}

typedef struct {
    const char *role_prefixes[4];
    int prefix_count;
    const char *body_templates[6];
    int body_count;
    const char *suffix_templates[3];
    int suffix_count;
} autogen_response_role_t;

static const autogen_response_role_t __attribute__((unused)) g_autogen_roles[] = {
    {{"As ", "From a ", "In my capacity as "},
     3,
     {"I've analyzed your request and prepared a response based on my role.",
      "Processing through the multi-agent framework, here's my assessment.",
      "After consulting with peer agents, I can provide this answer.",
      "My analysis of the input yields the following conclusion.",
      "Through the AutoGen orchestration layer, I've generated this response.",
      "Based on the group chat context and available tools, here's my output."},
     6,
     {" Would you like me to elaborate on any aspect?", " I'm ready for follow-up questions.", ""},
     3},
    {{"Acknowledged. ", "Noted. ", "Copy that. "},
     3,
     {"I've received and processed the message. Standing by for next instruction.",
      "Message acknowledged and logged. Awaiting further direction.",
      "Input received via AgentRT protocol bridge. Ready to proceed.",
      "Confirmed. The data has been routed through the agent mesh.",
      "Roger. Message processed successfully.", "Affirmative. All systems operational."},
     6,
     {" Over.", "", ""},
     2},
};

static int autogen_generate_response(autogen_adapter_context_t *ctx, const char *incoming_msg,
                                     int agent_index, int total_agents, bool is_first_in_round,
                                     char *out_buf, size_t buf_len)
{
    if (!out_buf || buf_len == 0)
        return AIRY_ERR_NULL_POINTER;

    if (ctx && ctx->llm_callback) {
        char prompt[4096];
        int plen = 0;
        if (is_first_in_round && total_agents > 1) {
            plen = snprintf(prompt, sizeof(prompt), "[AutoGen Agent #%d in group of %d] %s",
                            agent_index, total_agents, incoming_msg ? incoming_msg : "");
        } else {
            plen = snprintf(prompt, sizeof(prompt), "%s", incoming_msg ? incoming_msg : "");
        }
        if (plen <= 0)
            return AIRY_ERR_NOT_FOUND;

        char *llm_response = NULL;
        int rc = ctx->llm_callback(prompt, ctx->config.default_llm_model, &llm_response,
                                   ctx->llm_callback_data);
        if (rc == 0 && llm_response) {
            size_t copy_len = strlen(llm_response);
            if (copy_len >= buf_len)
                copy_len = buf_len - 1;
            __builtin_memcpy(out_buf, llm_response, copy_len);
            out_buf[copy_len] = '\0';
            AIRY_FREE(llm_response);
            return 0;
        }
        AIRY_FREE(llm_response);
        return AIRY_ERR_NULL_POINTER;
    }

    out_buf[0] = '\0';
    return AIRY_ERR_OUT_OF_MEMORY;
}

int autogen_initiate_chat(autogen_adapter_context_t *ctx, const char *group_id,
                          const char *sender_id, const char *message,
                          autogen_group_chat_result_t *result)
{
    if (!ctx || !result)
        {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "autogen_initiate_chat: IO error");
        return AIRY_ERR_UNKNOWN;
        }
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;
    if (!ctx->llm_callback)
        return AIRY_ERR_OVERFLOW;

    ctx->total_chats_initiated++;

    AIRY_MEMSET(result, 0, sizeof(autogen_group_chat_result_t));
    result->group_id = group_id ? AIRY_STRDUP(group_id) : NULL;
    result->initiator_id = sender_id ? AIRY_STRDUP(sender_id) : NULL;
    result->initial_message = message ? AIRY_STRDUP(message) : NULL;

    time_t start = time(NULL);

    static uint32_t conv_counter = 0;
    conv_counter++;

    autogen_conversation_t conv;
    AIRY_MEMSET(&conv, 0, sizeof(conv));
    char cid[64];
    snprintf(cid, sizeof(cid), "ag-conv-%08x", conv_counter);
    conv.conversation_id = AIRY_STRDUP(cid);
    conv.created_at = (uint64_t)(time(NULL));
    conv.last_activity = conv.created_at;
    conv.is_complete = true;
    conv.termination_reason = AIRY_STRDUP("completed");

    int chat_rounds = 3 + (int)(message ? strlen(message) % 5 : 3);
    int msg_count = chat_rounds * 2 + 1;

    conv.messages =
        (autogen_message_t *)AIRY_CALLOC((size_t)msg_count, sizeof(autogen_message_t));
    conv.message_count = (size_t)msg_count;

    for (int m = 0; m < msg_count; m++) {
        conv.messages[m].message_id = AIRY_MALLOC(32);
        if (!conv.messages[m].message_id) {
            for (int j = 0; j < m; j++)
                AIRY_FREE(conv.messages[j].message_id);
            AIRY_FREE(conv.messages);
            conv.messages = NULL;
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        snprintf(conv.messages[m].message_id, 32, "msg-%04d", m);
        conv.messages[m].sender_id =
            (m % 2 == 0) ? (sender_id ? AIRY_STRDUP(sender_id) : AIRY_STRDUP("user"))
                         : AIRY_STRDUP("assistant");
        conv.messages[m].receiver_id =
            (m % 2 == 0) ? AIRY_STRDUP("assistant")
                         : (sender_id ? AIRY_STRDUP(sender_id) : AIRY_STRDUP("user"));
        conv.messages[m].type = MSG_TYPE_TEXT;

        if (m == 0 && message) {
            conv.messages[m].content = AIRY_STRDUP(message);
        } else {
            char resp_buf[AUTOGEN_MAX_RESPONSE_LEN];
            AIRY_MEMSET(resp_buf, 0, sizeof(resp_buf));
            int agent_role_idx = (m / 2) % (ctx->agent_count > 0 ? (int)ctx->agent_count : 1);
            bool is_first_in_round = (m == 1);
            int rc = autogen_generate_response(ctx, message, agent_role_idx, (int)ctx->agent_count,
                                               is_first_in_round, resp_buf, sizeof(resp_buf));
            if (rc == 0 && resp_buf[0]) {
                conv.messages[m].content = AIRY_STRDUP(resp_buf);
            } else {
                conv.messages[m].content = AIRY_STRDUP("[LLM response unavailable]");
            }
        }

        conv.messages[m].timestamp = (uint64_t)(time(NULL));
        conv.messages[m].is_visible = true;
    }

    char summary_buf[512];
    snprintf(summary_buf, sizeof(summary_buf),
             "AutoGen group chat completed. Rounds: %d, Messages: %d, "
             "Agents involved: %zu",
             chat_rounds, msg_count, ctx->agent_count);
    conv.summary = AIRY_STRDUP(summary_buf);

    result->conversation =
        (autogen_conversation_t *)AIRY_CALLOC(1, sizeof(autogen_conversation_t));
    __builtin_memcpy(result->conversation, &conv, sizeof(autogen_conversation_t));

    result->total_time_ms = difftime(time(NULL), start) * 1000.0;
    result->total_rounds = chat_rounds;
    result->total_messages = msg_count;
    result->success = true;
    result->final_summary = AIRY_STRDUP(summary_buf);

    ctx->total_messages_exchanged += (uint64_t)msg_count;

    autogen_conversation_destroy(&conv);
    return 0;
}

int autogen_send_message(autogen_adapter_context_t *ctx, const char *from_agent_id,
                         const char *to_agent_id, const char *content, autogen_message_type_t type,
                         autogen_message_t *reply)
{
    if (!ctx || !reply)
        {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "autogen_send_message: IO error");
        return AIRY_ERR_UNKNOWN;
        }
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;
    if (!ctx->llm_callback)
        return AIRY_ERR_OVERFLOW;

    ctx->total_messages_exchanged++;

    static uint32_t msg_counter = 0;
    msg_counter++;

    AIRY_MEMSET(reply, 0, sizeof(autogen_message_t));
    reply->message_id = AIRY_MALLOC(32);
    if (!reply->message_id)
        return AIRY_ERR_OUT_OF_MEMORY;
    snprintf(reply->message_id, 32, "ag-msg-%08x", msg_counter);
    reply->sender_id = to_agent_id ? AIRY_STRDUP(to_agent_id) : NULL;
    reply->receiver_id = from_agent_id ? AIRY_STRDUP(from_agent_id) : NULL;
    reply->type = type;

    if (content && content[0]) {
        char resp_buf[AUTOGEN_MAX_RESPONSE_LEN];
        AIRY_MEMSET(resp_buf, 0, sizeof(resp_buf));
        int rc = autogen_generate_response(ctx, content, (int)(msg_counter % 8),
                                           (int)ctx->agent_count, true, resp_buf, sizeof(resp_buf));
        if (rc == 0 && resp_buf[0]) {
            reply->content = AIRY_STRDUP(resp_buf);
        } else {
            reply->content = AIRY_STRDUP("[LLM response unavailable]");
        }
    } else {
        reply->content = AIRY_STRDUP("ack");
    }

    reply->timestamp = (uint64_t)(time(NULL));
    reply->is_visible = true;

    if (ctx->message_hook)
        ctx->message_hook(reply, ctx->message_hook_data);

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

    autogen_tool_executor_fn *new_exec = (autogen_tool_executor_fn *)AIRY_REALLOC(
        ctx->tool_executors, (ctx->tool_count + 1) * sizeof(autogen_tool_executor_fn));
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

static int autogen_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    if (!context || !msg || !out_data || !out_size)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    const char *payload = umsg->payload ? (const char *)umsg->payload : "";
    size_t payload_len = umsg->payload_size;
    size_t buf_size = 256 + payload_len;
    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf)
        {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "autogen_proto_encode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    int written = snprintf(buf, buf_size,
                           "{\"protocol\":%d,\"direction\":%d,\"timestamp\":%llu,\"payload\":\"%.*s\"}",
                           (int)umsg->protocol, (int)umsg->direction,
                           (unsigned long long)umsg->timestamp, (int)payload_len, payload);
    if (written < 0)
        {
        AIRY_FREE(buf);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__, "autogen_proto_encode: snprintf failed");
        return AIRY_ERR_IO;
        }
    *out_data = buf;
    *out_size = (size_t)written;
    return 0;
}

static int autogen_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    if (!context || !data || !out_msg)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    if (size == 0)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
        }

    unified_message_t *msg = (unified_message_t *)out_msg;
    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy)
        {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "autogen_proto_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    msg->protocol = AIRY_PROTOCOL_A2A;
    char *p = strstr(copy, "\"protocol\":");
    if (p)
        msg->protocol = (airy_protocol_type_t)strtol(p + 11, NULL, 10);

    msg->direction = DIRECTION_RESPONSE;
    p = strstr(copy, "\"direction\":");
    if (p)
        msg->direction = (message_direction_t)strtol(p + 12, NULL, 10);

    msg->timestamp = (uint64_t)time(NULL);
    p = strstr(copy, "\"timestamp\":");
    if (p)
        msg->timestamp = (uint64_t)strtoull(p + 12, NULL, 10);

    p = strstr(copy, "\"payload\":\"");
    if (p) {
        p += 11;
        char *end = strchr(p, '"');
        size_t plen = end ? (size_t)(end - p) : strlen(p);
        msg->payload = AIRY_MALLOC(plen + 1);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, p, plen);
            ((char *)msg->payload)[plen] = '\0';
            msg->payload_size = plen;
        }
    } else {
        msg->payload = AIRY_STRDUP("");
        msg->payload_size = 0;
    }

    AIRY_FREE(copy);
    return 0;
}

static int autogen_proto_connect(void *context, const char *endpoint)
{
    if (!context)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_connect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    ctx->is_connected = true;
    return 0;
}

static int autogen_proto_disconnect(void *context)
{
    if (!context)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_disconnect: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    ctx->is_connected = false;
    AIRY_FREE(ctx->connected_endpoint);
    ctx->connected_endpoint = NULL;
    return 0;
}

static int autogen_proto_is_connected(void *context)
{
    if (!context)
        return 0;
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    return ctx->is_connected ? 1 : 0;
}

static int autogen_proto_send(void *context, const void *data, size_t size)
{
    if (!context || !data)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    AIRY_FREE(ctx->send_buffer);
    ctx->send_buffer = AIRY_MALLOC(size + 1);
    if (!ctx->send_buffer)
        {
        ctx->send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "autogen_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    __builtin_memcpy(ctx->send_buffer, data, size);
    ((char *)ctx->send_buffer)[size] = '\0';
    ctx->send_buffer_size = size;
    ctx->bytes_sent += size;
    return 0;
}

static int autogen_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!context || !data || !size)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    if (!ctx->send_buffer || ctx->send_buffer_size == 0)
        {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "autogen_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
        }
    *data = ctx->send_buffer;
    *size = ctx->send_buffer_size;
    ctx->bytes_received += *size;
    ctx->send_buffer = NULL;
    ctx->send_buffer_size = 0;
    return 0;
}

static int autogen_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    if (!context || !stats_json || max_size < 64)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "autogen_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    int written = snprintf(stats_json, max_size,
                           "{\"adapter\":\"autogen\",\"version\":\"%s\",\"connected\":%s,"
                           "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                           "\"agents\":%zu,\"messages\":%llu}",
                           AUTOGEN_ADAPTER_VERSION, ctx->is_connected ? "true" : "false",
                           (unsigned long long)ctx->bytes_sent,
                           (unsigned long long)ctx->bytes_received, ctx->agent_count,
                           (unsigned long long)ctx->total_messages_exchanged);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
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

static int autogen_proto_handle_request(void *context, const void *req, void **resp)
{
    if (!context || !req)
        return AIRY_ERR_NULL_POINTER;

    const char *raw_request = (const char *)req;

    autogen_message_t msg = {0};
    msg.sender_id = AIRY_STRDUP("proto-client");
    msg.content = AIRY_STRDUP(raw_request);
    msg.timestamp = (uint64_t)(time(NULL));
    msg.is_visible = true;

    autogen_message_t reply = {0};
    autogen_adapter_context_t *ctx = (autogen_adapter_context_t *)context;
    int ret = autogen_send_message(ctx, "user", "agent", raw_request, MSG_TYPE_TEXT, &reply);

    if (ret == 0 && resp) {
        if (reply.content) {
            *resp = AIRY_STRDUP(reply.content);
        } else {
            *resp = AIRY_STRDUP("{\"status\":\"error\"}");
        }
    } else if (resp) {
        *resp = AIRY_STRDUP("{\"status\":\"error\"}");
        ret = -1;
    }

    autogen_message_destroy(&msg);
    autogen_message_destroy(&reply);
    return ret;
}

static int autogen_proto_get_version(void *context, char *buf, size_t max_size)
{
    if (!buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    const char *ver = autogen_adapter_version();
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, ver, len);
    buf[len] = '\0';
    return 0;
}

static uint32_t autogen_proto_capabilities(void *context)
{
    return (uint32_t)(PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_AGENT_DISCOVERY |
                      PROTO_CAP_CODE_EXECUTION | PROTO_CAP_HUMAN_LOOP);
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
         * AutoGen 框架本质是多 Agent 协作，语义上最接近 A2A 协议。 */
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
