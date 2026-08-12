// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the A2A v0.3 adapter split files.
 */

#ifndef A2A_V03_ADAPTER_INTERNAL_H
#define A2A_V03_ADAPTER_INTERNAL_H

#include "a2a_v03_adapter.h"

/* Forward declarations for types defined in header */
typedef struct a2a_v03_adapter_s a2a_v03_adapter_t;

/* Internal agent card struct with fixed arrays (used internally by this file) */
typedef struct {
    char id[64];
    char name[128];
    char url[512];
    char capabilities[1024];
    int version;
    int protocol_version;
    bool available;
    char *capabilities_json;
    a2a_capability_t capabilities_mask;
} a2a_internal_card_t;

/* Transport write callback type for sending data through the transport layer */
typedef int (*a2a_transport_write_fn)(void *transport_ctx, const void *data, size_t size);

/* Use header-declared types; define local adapter state */
struct a2a_v03_adapter_s {
    a2a_internal_card_t agents[A2A_V03_MAX_AGENTS];
    size_t agent_count;
    uint64_t task_counter;
    size_t active_task_count;
    size_t completed_task_count;
    size_t failed_task_count;
    uint64_t total_delegation_ms;
    uint64_t total_consensus_ms;
    bool initialized;
    a2a_v03_config_t config;
    a2a_task_t *tasks[A2A_V03_MAX_TASKS];
    size_t task_count;
    a2a_notification_handler_t notification_handler;
    void *notification_handler_user_data;
    a2a_task_handler_t task_handler;
    void *task_handler_user_data;
    a2a_message_handler_t message_handler;
    void *message_handler_user_data;
    a2a_negotiation_handler_t negotiation_handler;
    void *negotiation_handler_user_data;
    a2a_streaming_handler_t streaming_handler;
    void *streaming_handler_user_data;
    /* Transport layer for sending data */
    a2a_transport_write_fn transport_write;
    void *transport_ctx;
    bool connected;
    uint64_t bytes_sent;
    uint64_t messages_sent;
};

/* Helpers shared across files (was static; now external linkage) **/
uint64_t a2a_timestamp_ms(void);

/* Compatibility defines shared across split files */
#define A2A_VERSION "0.3"

/* Protocol adapter callbacks (was static; referenced by a2a_v03_get_adapter()) **/
int a2a_adapter_init_cb(void *context);
int a2a_adapter_destroy_cb(void *context);
int a2a_adapter_encode_cb(void *c, const void *m, void **o, size_t *s);
int a2a_adapter_decode_cb(void *c, const void *d, size_t s, void *o);
int a2a_adapter_connect_cb(void *c, const char *e);
int a2a_adapter_disconnect_cb(void *c);
int a2a_adapter_is_connected_cb(void *c);
int a2a_adapter_send_cb(void *c, const void *d, size_t s);
int a2a_adapter_receive_cb(void *c, void **d, size_t *s, uint32_t t);
int a2a_adapter_handle_request_cb(void *c, const void *r, void **rp);
int a2a_adapter_get_version_cb(void *c, char *b, size_t s);
uint32_t a2a_adapter_capabilities_cb(void *c);
int a2a_adapter_get_stats_cb(void *c, char *b, size_t s);

#endif /* A2A_V03_ADAPTER_INTERNAL_H */
