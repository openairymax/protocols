// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file protocol_extension_framework_internal.h
 * @brief Internal types and cross-file declarations shared by the framework split files.
 */

#ifndef PROTOCOL_EXTENSION_FRAMEWORK_INTERNAL_H
#define PROTOCOL_EXTENSION_FRAMEWORK_INTERNAL_H

#include "protocol_extension_framework.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    proto_ext_descriptor_t descriptor;
    proto_ext_callbacks_t callbacks;
    void *adapter_context;
    proto_ext_state_t state;
    uint32_t error_count;
    uint64_t last_activity_ms;
    uint64_t messages_processed;
    bool registered;
} proto_ext_adapter_entry_t;

struct proto_ext_framework_s {
    proto_ext_adapter_entry_t *adapters;
    size_t adapter_count;
    size_t adapter_capacity;

    proto_middleware_t *middlewares;
    size_t middleware_count;
    size_t middleware_capacity;

    uint64_t total_messages;
};

/* Time helpers (was static; referenced by registry/route domains and built-ins) **/
uint64_t current_time_ms(void);

#endif /* PROTOCOL_EXTENSION_FRAMEWORK_INTERNAL_H */
