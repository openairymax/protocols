// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file autogen_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the AutoGen adapter split files.
 */

#ifndef AUTOGEN_ADAPTER_INTERNAL_H
#define AUTOGEN_ADAPTER_INTERNAL_H

#include "autogen_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Message codec domain (was static; referenced by group-chat and protocol-callback domains) **/
int autogen_generate_response(autogen_adapter_context_t *ctx, const char *incoming_msg,
                              int agent_index, int total_agents, bool is_first_in_round,
                              char *out_buf, size_t buf_len);

/* Protocol adapter callbacks (was static; referenced by autogen_get_protocol_adapter()) **/
int autogen_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size);
int autogen_proto_decode(void *context, const void *data, size_t size, void *out_msg);
int autogen_proto_connect(void *context, const char *endpoint);
int autogen_proto_disconnect(void *context);
int autogen_proto_is_connected(void *context);
int autogen_proto_send(void *context, const void *data, size_t size);
int autogen_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms);
int autogen_proto_get_stats(void *context, char *stats_json, size_t max_size);
int autogen_proto_handle_request(void *context, const void *req, void **resp);
int autogen_proto_get_version(void *context, char *buf, size_t max_size);
uint32_t autogen_proto_capabilities(void *context);

#endif /* AUTOGEN_ADAPTER_INTERNAL_H */
