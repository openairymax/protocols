/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file airy_protocol_interface.h
 * @brief AgentRT protocol system unified interface definition.
 *
 * Defines the public interface contract of the AgentRT protocol system, the
 * unified abstraction layer for all protocol adapters, gateways and SDKs.
 *
 * Moved from agentrt/interfaces/include/ to agentrt/protocols/include/
 * (2026-04-19 interfaces removal refactor).
 */

#ifndef AIRY_RT_PROTOCOL_INTERFACE_H
#define AIRY_RT_PROTOCOL_INTERFACE_H

#include "unified_protocol.h"

#include "airy_abi.h"

AIRY_ABI_BEGIN

/* unified_message_t and protocol_type_t now defined in unified_protocol.h */
/* ============================================================================
  * I-L1: Protocol Adapter Interface (basic adapter interface)
 * ============================================================================ */

typedef enum {
    PROTO_CAP_SYNC_REQUEST = 0x0001,
    PROTO_CAP_REQUEST_RESPONSE = 0x0001,
    PROTO_CAP_STREAMING = 0x0002,
    PROTO_CAP_BIDIRECTIONAL = 0x0004,
    PROTO_CAP_TOOL_DISCOVERY = 0x0008,
    PROTO_CAP_RESOURCE_ACCESS = 0x0010,
    PROTO_CAP_AGENT_DISCOVERY = 0x0020,
    PROTO_CAP_TASK_LIFECYCLE = 0x0040,
    PROTO_CAP_FUNCTION_CALLING = 0x0080,
    PROTO_CAP_EMBEDDINGS = 0x0100,
    PROTO_CAP_AUTHENTICATION = 0x0200,
    PROTO_CAP_RATE_LIMITING = 0x0400,
    PROTO_CAP_NEGOTIATION = 0x0800,
    PROTO_CAP_NOTIFICATIONS = 0x1000,
    PROTO_CAP_CUSTOM = 0x8000,
    PROTO_CAP_BATCH = 0x00010000,
    PROTO_CAP_TOOL_CALLING = 0x00020000,
    PROTO_CAP_CONSENSUS = 0x00040000,
    PROTO_CAP_BINARY = 0x00080000,
    PROTO_CAP_LOW_LATENCY = 0x00100000,
    PROTO_CAP_CRC_CHECKSUM = 0x00200000,
    PROTO_CAP_MULTIMODAL = 0x00400000,
    PROTO_CAP_VISION = 0x00800000,
    PROTO_CAP_EXTENDED_THINKING = 0x01000000,
    PROTO_CAP_CODE_EXECUTION = 0x02000000,
    PROTO_CAP_HUMAN_LOOP = 0x04000000
} proto_capability_flags_t;

typedef enum {
    PROTO_CONN_DISCONNECTED = 0,
    PROTO_CONN_CONNECTING,
    PROTO_CONN_CONNECTED,
    PROTO_CONN_RECONNECTING,
    PROTO_CONN_ERROR,
    PROTO_CONN_CLOSED
} proto_connection_state_t;

typedef struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t errors_total;
    uint64_t errors_timeout;
    uint64_t errors_protocol;
    double avg_latency_ms;
    double p50_latency_ms;
    double p99_latency_ms;
    uint32_t active_connections;
    proto_connection_state_t connection_state;
} proto_stats_t;

typedef struct {
    int (*init)(void **context);
    void (*destroy)(void *context);
    int (*encode)(void *context, const unified_message_t *in_msg, void **out_data,
                  size_t *out_size);
    int (*decode)(void *context, const void *in_data, size_t in_size, unified_message_t *out_msg);
    int (*connect)(void *context, const char *address);
    int (*disconnect)(void *context);
    bool (*is_connected)(void *context);
    int (*send)(void *context, const unified_message_t *message);
    int (*receive)(void *context, unified_message_t *message, int timeout_ms);
    int (*get_stats)(void *context, proto_stats_t *stats);
    const char *(*get_name)(void);
    protocol_type_t (*get_type)(void);
    uint32_t (*get_capabilities)(void);
} proto_adapter_vtable_t;

typedef struct proto_adapter_entry_s {
    const char *name;
    const char *version;
    const char *description;
    protocol_type_t type;
    uint32_t capabilities;
    const proto_adapter_vtable_t *vtable;
    bool is_builtin;
    struct proto_adapter_entry_s *next;
} proto_adapter_entry_t;

/* ============================================================================
  * Global registration and discovery APIs
  * ============================================================================ */

int proto_interface_register_builtins(void);
const proto_adapter_entry_t *proto_interface_find(const char *name);
int proto_interface_list_all(char **json_output);
const char *proto_interface_type_name(protocol_type_t type);
protocol_type_t proto_interface_parse_type(const char *name);

AIRY_ABI_END

#endif /* AIRY_RT_PROTOCOL_INTERFACE_H */
