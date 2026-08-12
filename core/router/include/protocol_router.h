/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_router.h
 * @brief Protocol routing and transformation engine.
 *
 * Adaptive routing and transformation for MCP/A2A/OpenAI API and other
 * protocols. Provides smart protocol routing, message transformation and
 * protocol compatibility handling.
 */

#ifndef AIRY_RT_PROTOCOL_ROUTER_H
#define AIRY_RT_PROTOCOL_ROUTER_H

#include "unified_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Protocol conversion rule
 */
typedef struct {
    protocol_type_t source_protocol;
    protocol_type_t target_protocol;
    const char *source_endpoint;
    const char *target_endpoint;
    uint32_t priority;
    void *transformer_context;
} protocol_rule_t;

/**
  * @brief Message conversion function type
  * @param source Source message
  * @param target Target message (output)
  * @param context Transformer context
  * @return Conversion result: 0 on success, negative error codes
 */
typedef int (*message_transformer_t)(const unified_message_t *source, unified_message_t *target,
                                     void *context);

/**
  * @brief Route decision function type
  * @param message Input message
  * @param rules Rule list
  * @param rule_count Rule count
  * @return Selected rule index, -1 if none matched
 */
typedef int (*route_decision_func_t)(const unified_message_t *message, const protocol_rule_t *rules,
                                     size_t rule_count);

/**
  * @brief Route engine handle
 */
typedef struct protocol_router_s *protocol_router_handle_t;

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Create a route engine instance
  * @param default_protocol Default protocol (used when no rule matches)
  * @return Route engine handle, or NULL on failure
 */
protocol_router_handle_t protocol_router_create(protocol_type_t default_protocol);

/**
  * @brief Destroy a route engine instance
  * @param router Route engine handle
 */
void protocol_router_destroy(protocol_router_handle_t router);

/**
  * @brief Add a protocol conversion rule
  * @param router Route engine handle
  * @param rule Conversion rule
  * @param transformer Message conversion function (may be NULL for the default)
  * @return 0 on success, negative error codes on failure
 */
int protocol_router_add_rule(protocol_router_handle_t router, const protocol_rule_t *rule,
                             message_transformer_t transformer);

/**
  * @brief Remove conversion rules matching the source endpoint
 *
  * Removes all rules whose source_endpoint equals the pattern
  * (real deletion that frees nodes, not a stub).
 *
  * @param router Route engine handle
  * @param source_endpoint_pattern Source endpoint pattern (as passed to add_rule)
  * @return 0 on success; AIRY_ERR_NOT_FOUND if none match; AIRY_EINVAL on invalid args
 */
int protocol_router_remove_rule(protocol_router_handle_t router,
                                const char *source_endpoint_pattern);

/**
  * @brief Route a message
  * @param router Route engine handle
  * @param message Input message
  * @param transformed Transformed message (output)
  * @return 0 on success, negative error codes on failure
 *
  * @note If no rule matches, pass the message through with the default protocol (no conversion)
 */
int protocol_router_route(protocol_router_handle_t router, const unified_message_t *message,
                          unified_message_t *transformed);

/**
  * @brief Route a batch of messages
  * @param router Route engine handle
  * @param messages Input message array
  * @param count Message count
  * @param transformed Transformed message array (output)
  * @return Messages routed successfully, negative on error
 */
int protocol_router_route_batch(protocol_router_handle_t router, const unified_message_t *messages,
                                size_t count, unified_message_t *transformed);

/**
  * @brief Set the route decision function
  * @param router Route engine handle
  * @param decision_func Route decision function
  * @return 0 on success, negative error codes on failure
 */
int protocol_router_set_decision_func(protocol_router_handle_t router,
                                      route_decision_func_t decision_func);

/**
  * @brief Get route statistics
  * @param router Route engine handle
  * @param stats_json Statistics JSON string (output; caller frees)
  * @return 0 on success, negative error codes on failure
 */
int protocol_router_get_stats(protocol_router_handle_t router, char **stats_json);

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief JSON-RPC to MCP converter
  * @param source Source message (JSON-RPC)
  * @param target Target message (MCP)
  * @param context Transformer context
  * @return 0 on success, negative error codes on failure
 */
int protocol_transformer_jsonrpc_to_mcp(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief MCP to JSON-RPC converter
  * @param source Source message (MCP)
  * @param target Target message (JSON-RPC)
  * @param context Transformer context
  * @return 0 on success, negative error codes on failure
 */
int protocol_transformer_mcp_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief OpenAI API to JSON-RPC converter
  * @param source Source message (OpenAI API)
  * @param target Target message (JSON-RPC)
  * @param context Transformer context
  * @return 0 on success, negative error codes on failure
 */
int protocol_transformer_openai_to_jsonrpc(const unified_message_t *source,
                                           unified_message_t *target, void *context);

/**
  * @brief A2A to JSON-RPC converter
  * @param source Source message (A2A)
  * @param target Target message (JSON-RPC)
  * @param context Transformer context
  * @return 0 on success, negative error codes on failure
 */
int protocol_transformer_a2a_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
  * @brief Default message converter (direct copy, no conversion)
  * @param source Source message
 * @param target Target message
  * @param context Transformer context (ignored)
  * @return 0 on success, negative error codes on failure
 */
int protocol_transformer_default(const unified_message_t *source, unified_message_t *target,
                                 void *context);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_ROUTER_H */
