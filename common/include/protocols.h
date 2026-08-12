/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocols.h
 * @brief Main header for the AgentRT unified protocols framework.
 *
 * Master header of the unified protocol stack framework, containing all
 * public APIs and type definitions for protocol-agnostic communication.
 */

#ifndef AIRY_RT_PROTOCOLS_H
#define AIRY_RT_PROTOCOLS_H

#include "unified_protocol.h"

/**
 * @defgroup protocols Unified Protocols Framework
  * @brief Unified protocol stack framework
 *
  * Provides a unified abstraction across HTTP, WebSocket, gRPC, MQTT, etc.
 *
  * Key features:
  * 1. Protocol-agnostic API design
  * 2. Unified message model
  * 3. High-performance message routing
  * 4. Extensible adapter architecture
  * 5. Built-in connection pool and load balancing
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Initialize the protocol stack framework
  * @note Must be called before any other protocol stack function
  * @return 0 on success, negative error codes on failure
 */
int protocols_framework_init(void);

/**
  * @brief Clean up the protocol stack framework
  * @note Call before program exit
 */
void protocols_framework_cleanup(void);

/**
  * @brief Get the framework version
  * @return Version string
 */
const char *protocols_framework_version(void);

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Protocol stack manager handle
 */
typedef struct protocol_manager_s *protocol_manager_handle_t;

/**
  * @brief Create a protocol stack manager
  * @return Manager handle, or NULL on failure
 */
protocol_manager_handle_t protocol_manager_create(void);

/**
  * @brief Destroy a protocol stack manager
  * @param manager Manager handle
 */
void protocol_manager_destroy(protocol_manager_handle_t manager);

/**
  * @brief Create a protocol stack via the manager
  * @param manager Manager handle
  * @param config Protocol stack configuration
  * @return Protocol stack handle, or NULL on failure
 */
protocol_stack_handle_t protocol_manager_create_stack(protocol_manager_handle_t manager,
                                                      const protocol_stack_config_t *config);

/**
  * @brief Destroy a protocol stack via the manager
  * @param manager Manager handle
  * @param stack Protocol stack handle
 */
void protocol_manager_destroy_stack(protocol_manager_handle_t manager,
                                    protocol_stack_handle_t stack);

/**
  * @brief List all protocol stacks
  * @param manager Manager handle
  * @param stacks Protocol stack handle array (output)
  * @param max_count Maximum array capacity
  * @return Actual protocol stack count
 */
size_t protocol_manager_get_stacks(protocol_manager_handle_t manager,
                                   protocol_stack_handle_t *stacks, size_t max_count);

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Get the HTTP/JSON-RPC protocol adapter
  * @return HTTP (JSON-RPC) protocol adapter
 */
const protocol_adapter_t *protocol_adapter_http(void);

/* P0-15: removed protocol_adapter_websocket/grpc/mqtt declarations.
  * Those legacy transport adapters used the removed PROTOCOL_WEBSOCKET/GRPC/MQTT macros,
  * incompatible with the app-layer protocol enums and unused (gateway implements its own). */

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Protocol stack error codes
 */
typedef enum {
    PROTOCOL_SUCCESS = 0,
    PROTOCOL_ERROR_INVALID_ARG,
    PROTOCOL_ERROR_MEMORY,
    PROTOCOL_ERROR_NOT_INITIALIZED,
    PROTOCOL_ERROR_NOT_CONNECTED,
    PROTOCOL_ERROR_TIMEOUT,
    PROTOCOL_ERROR_ENCODE,
    PROTOCOL_ERROR_DECODE,
    PROTOCOL_ERROR_NETWORK,
    PROTOCOL_ERROR_PROTOCOL,
    PROTOCOL_ERROR_INTERNAL
} protocol_error_t;

/**
  * @brief Get the error code description
  * @param error Error code
  * @return Error description string
 */
const char *protocol_error_to_string(protocol_error_t error);

/**
  * @brief Get the last error message
  * @return Error message string
 */
const char *protocol_get_last_error(void);

/* ============================================================================ */

/* ============================================================================ */
/**
  * @brief Create a default protocol stack configuration
  * @param name Protocol stack name
  * @return Default configuration
 */
protocol_stack_config_t protocol_stack_config_default(const char *name);

/**
  * @brief Free protocol stack configuration resources
  * @param config Configuration structure
 */
void protocol_stack_config_destroy(protocol_stack_config_t *config);

#ifdef __cplusplus
}
#endif

/** @} */ /* end of protocols group */
#endif /* AIRY_RT_PROTOCOLS_H */