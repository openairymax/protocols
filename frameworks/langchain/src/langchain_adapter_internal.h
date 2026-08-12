// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file langchain_adapter_internal.h
 * @brief Cross-file declarations shared by the LangChain adapter split files.
 *
 * @note langchain_adapter_context_s is fully defined in the public header
 * langchain_adapter.h; it does not need to be redeclared here.
 */

#ifndef LANGCHAIN_ADAPTER_INTERNAL_H
#define LANGCHAIN_ADAPTER_INTERNAL_H

#include "langchain_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Chain execution domain (was static; referenced by agent-exec and protocol-callback domains) **/
int lc_generate_chain_response(langchain_adapter_context_t *ctx, const char *input_json,
                               size_t tool_count, bool is_agent_mode, char *out_buf,
                               size_t buf_len);
int lc_word_count(const char *t);

/* Protocol adapter callbacks (was static; referenced by langchain_get_protocol_adapter()) **/
int langchain_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size);
int langchain_proto_decode(void *context, const void *data, size_t size, void *out_msg);
int langchain_proto_connect(void *context, const char *endpoint);
int langchain_proto_disconnect(void *context);
int langchain_proto_is_connected(void *context);
int langchain_proto_send(void *context, const void *data, size_t size);
int langchain_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms);
int langchain_proto_get_stats(void *context, char *stats_json, size_t max_size);
int langchain_proto_handle_request(void *context, const void *req, void **resp);

#endif /* LANGCHAIN_ADAPTER_INTERNAL_H */
