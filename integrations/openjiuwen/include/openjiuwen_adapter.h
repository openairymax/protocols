/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file openjiuwen_adapter.h
 * @brief OpenJiuwen protocol adapter interface.
 *
 * Provides a protocol compatibility layer with the OpenJiuwen platform,
 * supporting message format conversion and interoperability.
 */

#ifndef OPENJIUWEN_ADAPTER_H
#define OPENJIUWEN_ADAPTER_H

#include "protocol_extension_framework.h"
#include "unified_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================

 * ============================================================================ */

#define OPENJIUWEN_PROTOCOL_VERSION "1.0.0"
#define OPENJIUWEN_MAX_MESSAGE_SIZE (64 * 1024)
#define OPENJIUWEN_DEFAULT_ENDPOINT "/openjiuwen/v1"
#define OPENJIUWEN_TIMEOUT_MS 30000
#define OPENJIUWEN_HEARTBEAT_INTERVAL_SEC 30
#define OPENJIUWEN_MAX_CONSECUTIVE_ERRORS 5
#define OPENJIUWEN_RECONNECT_BASE_DELAY_MS 1000
#define OPENJIUWEN_RECONNECT_MAX_DELAY_MS 60000
#define OPENJIUWEN_SEND_QUEUE_SIZE 64

typedef enum {
    OPENJIUWEN_CONN_DISCONNECTED = 0,
    OPENJIUWEN_CONN_CONNECTING,
    OPENJIUWEN_CONN_CONNECTED,
    OPENJIUWEN_CONN_RECONNECTING,
    OPENJIUWEN_CONN_ERROR
} openjiuwen_conn_state_t;

/* ============================================================================

 * ============================================================================ */

/**
  * @brief OpenJiuwen configuration structure
 */
typedef struct openjiuwen_config_s {
    char endpoint[256];
    char api_key[128];
    int timeout_ms;
    bool enable_compression;
    bool enable_encryption;
    uint32_t max_retries;
} openjiuwen_config_t;

/**
  * @brief OpenJiuwen message header
 */
typedef struct openjiuwen_header_s {
    uint32_t message_id;
    uint32_t timestamp;
    uint16_t message_type;
    uint16_t flags;
    uint32_t payload_length;
    char source_agent[64];
    char target_agent[64];
} openjiuwen_header_t;

/**
  * @brief OpenJiuwen message type enumeration
 */
typedef enum openjiuwen_message_type_e {
    OPENJIUWEN_MSG_TYPE_REQUEST = 0x0001,
    OPENJIUWEN_MSG_TYPE_RESPONSE = 0x0002,
    OPENJIUWEN_MSG_TYPE_NOTIFICATION = 0x0003,
    OPENJIUWEN_MSG_TYPE_HEARTBEAT = 0x0004,
    OPENJIUWEN_MSG_TYPE_ERROR = 0x0005
} openjiuwen_message_type_t;

/**
  * @brief OpenJiuwen adapter instance (extends openjiuwen_base_adapter_t)
 */
typedef struct openjiuwen_adapter_s {
    protocol_adapter_t base;
    openjiuwen_config_t config;
    void *connection_handle;
    bool initialized;
    uint32_t message_counter;
    void *user_data;
    openjiuwen_conn_state_t conn_state;
    uint32_t consecutive_errors;
    uint32_t total_reconnects;
    uint32_t last_heartbeat_sec;
    uint32_t last_error_code;
    uint64_t last_activity_ms;
} openjiuwen_adapter_t;

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Create an OpenJiuwen adapter instance
 *
  * @param config OpenJiuwen configuration (NULL for defaults)
  * @return New adapter instance, or NULL on failure
 */
const protocol_adapter_t *openjiuwen_adapter_create(const openjiuwen_config_t *config);

/**
  * @brief Initialize the default configuration
 *
  * @param config Output configuration struct pointer
 */
void openjiuwen_get_default_config(openjiuwen_config_t *config);

/**
  * @brief Validate the OpenJiuwen connection
 *
  * @param adapter Adapter instance
  * @return 0 on success, non-zero error codes
 */
int openjiuwen_verify_connection(const protocol_adapter_t *adapter);

/**
  * @brief Get the protocol capabilities supported by OpenJiuwen
 *
  * @param adapter Adapter instance
  * @param capabilities Output capability description string
  * @param max_len Maximum string buffer length
  * @return 0 on success, non-zero error codes
 */
int openjiuwen_get_capabilities(const protocol_adapter_t *adapter, char *capabilities,
                                size_t max_len);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Convert a unified_message to OpenJiuwen format
 *
  * @param msg Unified message format
  * @param out_buffer Output buffer
  * @param buffer_size Buffer size
  * @return Bytes written, negative on error
 */
int openjiuwen_unified_to_native(const unified_message_t *msg, void *out_buffer,
                                 size_t buffer_size);

/**
  * @brief Convert OpenJiuwen format to unified_message
 *
  * @param in_buffer Input buffer
  * @param buffer_size Input data size
  * @param msg Output unified message format
  * @return 0 on success, non-zero error codes
 */
int openjiuwen_native_to_unified(const void *in_buffer, size_t buffer_size, unified_message_t *msg);

/* ============================================================================

 * ============================================================================ */

/**
  * @brief Standard interface definition of the OpenJiuwen adapter
 *
  * This global can be passed directly to unified_protocol_register_adapter()
 */
extern const protocol_adapter_t openjiuwen_adapter_interface;

#ifdef __cplusplus
}
#endif

#endif /* OPENJIUWEN_ADAPTER_H */