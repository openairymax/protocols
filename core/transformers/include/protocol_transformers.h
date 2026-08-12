/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_transformers.h
 * @brief Protocol message transformers (complete implementation).
 *
 * Implements bidirectional message conversion between all protocols
 * supported by AgentRT:
 * - JSON-RPC 2.0 <-> MCP v1.0
 * - JSON-RPC 2.0 <-> A2A v0.3
 * - JSON-RPC 2.0 <-> OpenAI API
 * - JSON-RPC 2.0 <-> OpenJiuwen
 *
 * Conversion rules follow Capital_Specifications/airy_contract/protocol_contract.md
 *
 * @since 0.1.0
 */

#ifndef AIRY_RT_PROTOCOL_TRANSFORMERS_H
#define AIRY_RT_PROTOCOL_TRANSFORMERS_H

#include "unified_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
  * Conversion context - carries protocol-specific metadata to aid conversion
 * ============================================================================ */

typedef struct {
    char source_protocol[32];
    char target_protocol[32];
    char agent_id[64];
    char session_id[64];
    char trace_id[64];
    int jsonrpc_id_counter;
} transform_context_t;

transform_context_t *transform_context_create(const char *src_proto, const char *tgt_proto);
void transform_context_destroy(transform_context_t *ctx);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Convert a JSON-RPC tools/call request to MCP tools/call format
 *
  * Mapping rules:
 *   jsonrpc.method = "skill.execute" -> mcp.method = "tools/call"
 *   jsonrpc.params.name -> mcp.params.name
 *   jsonrpc.params.arguments -> mcp.params.arguments
 */
int transformer_jsonrpc_to_mcp_request(const unified_message_t *source, unified_message_t *target,
                                       void *context);

/**
  * @brief Convert an MCP tools/call response to JSON-RPC format
 *
  * Mapping rules:
 *   mcp.result.content[] -> jsonrpc.result.output
 *   mcp.error -> jsonrpc.error
 */
int transformer_mcp_to_jsonrpc_response(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief Convert an MCP tools/list response to JSON-RPC skill.list format
 */
int transformer_mcp_tools_list_to_jsonrpc(const unified_message_t *source,
                                          unified_message_t *target, void *context);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Convert a JSON-RPC task.submit request to A2A task/delegate format
 */
int transformer_jsonrpc_to_a2a_task(const unified_message_t *source, unified_message_t *target,
                                    void *context);

/**
  * @brief Convert an A2A task response to JSON-RPC format
 */
int transformer_a2a_to_jsonrpc_response(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief Convert a JSON-RPC agent.discover request to A2A agent/discover format
 */
int transformer_jsonrpc_to_a2a_discover(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief Convert an A2A agent card list to JSON-RPC agent.list format
 */
int transformer_a2a_agents_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                      void *context);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Convert a JSON-RPC llm.complete request to OpenAI /v1/chat/completions format
 *
  * Mapping rules:
 *   jsonrpc.params.messages -> openai.messages (role/content)
 *   jsonrpc.params.model -> openai.model
 *   jsonrpc.params.temperature -> openai.temperature
 *   jsonrpc.params.max_tokens -> openai.max_tokens
 *   jsonrpc.params.tools -> openai.tools/functions[]
 */
int transformer_jsonrpc_to_openai_chat(const unified_message_t *source, unified_message_t *target,
                                       void *context);

/**
  * @brief Convert an OpenAI chat completions response to JSON-RPC format
 *
  * Mapping rules:
 *   openai.choices[0].message.content -> jsonrpc.result.content
 *   openai.choices[0].finish_reason -> jsonrpc.result.finish_reason
 *   openai.usage -> jsonrpc.result.usage
 */
int transformer_openai_chat_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                       void *context);

/**
  * @brief Convert an OpenAI streaming chunk to a JSON-RPC notification
 */
int transformer_openai_stream_chunk_to_jsonrpc(const unified_message_t *source,
                                               unified_message_t *target, void *context);

/**
  * @brief Convert a JSON-RPC embedding request to OpenAI /v1/embeddings format
 */
int transformer_jsonrpc_to_openai_embedding(const unified_message_t *source,
                                            unified_message_t *target, void *context);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Convert a JSON-RPC request to the OpenJiuwen binary format
 *
  * OpenJiuwen uses a custom binary protocol:
 *   Header(24B) + Payload(variable) + CRC32(4B)
 */
int transformer_jsonrpc_to_openjiuwen(const unified_message_t *source, unified_message_t *target,
                                      void *context);

/**
  * @brief Convert an OpenJiuwen response to JSON-RPC format
 */
int transformer_openjiuwen_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                      void *context);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Automatically select a converter by source and target protocol
 *
  * Protocol mapping table:
 *   HTTP(JSON-RPC) + endpoint /mcp/(*)     -> MCP
 *   HTTP(JSON-RPC) + endpoint /a2a/(*)     -> A2A
 *   HTTP(JSON-RPC) + endpoint /v1/chat/(*) -> OpenAI
 *   HTTP(JSON-RPC) + endpoint /ojw/(*)     -> OpenJiuwen
 */
int protocol_auto_transform(const unified_message_t *source, unified_message_t *target,
                            const char *target_protocol_name);

/**
  * @brief Validate the integrity of a converted message
 */
int protocol_validate_transformed(const unified_message_t *msg);

/**
  * @brief List the names of all registered converters
 */
const char **protocol_list_transformers(size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_TRANSFORMERS_H */
