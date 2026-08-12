// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file claude_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the Claude adapter split files.
 */

#ifndef CLAUDE_ADAPTER_INTERNAL_H
#define CLAUDE_ADAPTER_INTERNAL_H

#include "claude_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLAUDE_MAX_RESPONSE_LEN 4096
#define CLAUDE_STREAM_CHUNK_SIZE 10

struct claude_adapter_context_s {
    claude_config_t config;
    bool initialized;
    bool is_connected;
    char *connected_endpoint;
    void *send_buffer;
    size_t send_buffer_size;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    claude_message_handler_t message_handler;
    void *message_handler_data;
    claude_stream_handler_t stream_handler;
    void *stream_handler_data;
    claude_tool_use_handler_t tool_use_handler;
    void *tool_use_handler_data;
    uint64_t total_requests;
    uint64_t total_tokens_in;
    uint64_t total_tokens_out;
    uint64_t total_tool_calls;
    char last_error[256];
};

/* Global singleton protocol context (was static; referenced by the API-request domain) **/
extern void *g_claude_proto_context;

/* Built-in model catalog (was static; defined by the model domain, read by stats) **/
extern claude_model_info_t g_builtin_models[];
extern const int g_builtin_model_count;

/* Helpers shared across files (was static; now external linkage) **/
const char *claude_model_id_to_api_name(claude_model_id_t id);
int claude_estimate_tokens(const char *text);
int claude_generate_response(const char *user_msg, const char *system_ctx, char *out_buf,
                             size_t buf_len);
int claude_api_call(const char *api_key, const char *base_url, const char *request_json,
                    char *out_buf, size_t buf_len);

/* Protocol adapter callbacks (was static; referenced by claude_get_protocol_adapter()) **/
int claude_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size);
int claude_proto_decode(void *context, const void *data, size_t size, void *out_msg);
int claude_proto_connect(void *context, const char *endpoint);
int claude_proto_disconnect(void *context);
int claude_proto_is_connected(void *context);
int claude_proto_send(void *context, const void *data, size_t size);
int claude_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms);
int claude_proto_get_stats(void *context, char *stats_json, size_t max_size);

#endif /* CLAUDE_ADAPTER_INTERNAL_H */
