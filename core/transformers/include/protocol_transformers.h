/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_transformers.h
 * @brief Mechanism-core protocol message transformers.
 *
 * Implements bidirectional conversion for the open-standard protocols carried
 * by the mechanism core:
 * - JSON-RPC 2.0 <-> MCP v1.0
 * - JSON-RPC 2.0 <-> A2A v0.3
 *
 * Vendor/ecosystem protocol transforms are supplied by the assembly layer
 * through the proto_catalog_transforms() port (see protocol_catalog.h); this
 * header declares the neutral transform descriptor they are registered with.
 * Mechanism/strategy separation per 0.1.19 architecture plan §4.7/§5.1.
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
 * Neutral transform descriptor - the mechanism/strategy injection seam.
 *
 * The mechanism core ships the standard transforms in its own table; the
 * assembly layer contributes vendor transforms as an array of these
 * descriptors, keyed by (from_proto, to_proto). No vendor knowledge lives in
 * the core (see 0.1.19 architecture plan §4.7/§5.1).
 * ============================================================================ */

typedef int (*proto_transform_fn_t)(const unified_message_t *source, unified_message_t *target,
                                    void *context);

typedef struct {
    const char *from_proto;
    const char *to_proto;
    proto_transform_fn_t transform;
} proto_transform_def_t;

/* ============================================================================
 * MCP transforms (open standard)
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
 * A2A transforms (open standard)
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
 * Auto-transform dispatcher
 * ============================================================================ */

/**
  * @brief Automatically select a converter by source and target protocol
  *
  * Looks up the core standard transform table first, then consults the
  * assembly-layer port proto_catalog_transforms() for vendor transforms.
  * Falls back to a direct copy when no transform matches.
 */
int protocol_auto_transform(const unified_message_t *source, unified_message_t *target,
                            const char *target_protocol_name);

/**
  * @brief Validate the integrity of a converted message
 */
int protocol_validate_transformed(const unified_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_TRANSFORMERS_H */
