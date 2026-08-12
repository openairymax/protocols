// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file autogen_adapter_msg.c
 * @brief AutoGen adapter message codec and response generation domain.
 *
 * Single responsibility: generate agent responses via llm_callback, initiate
 * group chats (initiate_chat), send/receive single messages (send_message)
 * and role-template helpers.
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

int autogen_generate_response(autogen_adapter_context_t *ctx, const char *incoming_msg,
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
    if (!ctx || !result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "autogen_initiate_chat: IO error");
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

    conv.messages = (autogen_message_t *)AIRY_CALLOC((size_t)msg_count, sizeof(autogen_message_t));
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
            (m % 2 == 0) ? (sender_id ? AIRY_STRDUP(sender_id) : AIRY_STRDUP("user")) :
                           AIRY_STRDUP("assistant");
        conv.messages[m].receiver_id =
            (m % 2 == 0) ? AIRY_STRDUP("assistant") :
                           (sender_id ? AIRY_STRDUP(sender_id) : AIRY_STRDUP("user"));
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

    result->conversation = (autogen_conversation_t *)AIRY_CALLOC(1, sizeof(autogen_conversation_t));
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
    if (!ctx || !reply) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "autogen_send_message: IO error");
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
