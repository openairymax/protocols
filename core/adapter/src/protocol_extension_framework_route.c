// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_extension_framework_route.c
 * @brief Protocol extension framework message routing and negotiation domain.
 *
 * Single responsibility: route message send/request handling to the target
 * extension, auto-route by protocol type and negotiate protocol versions.
 */

#define LOG_TAG "protocol_extension_framework"

#include "protocol_extension_framework.h"
#include "protocol_extension_framework_internal.h"

#include "airy_memory.h"
#include "types.h"

#include <stdio.h>
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

int proto_ext_send_message(proto_ext_framework_t *fw, const char *adapter_name,
                           const unified_message_t *message)
{
    if (!fw || !adapter_name || !message) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_send_message: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, adapter_name) == 0) {
            if (fw->adapters[i].state != PROTO_EXT_STATE_RUNNING)
                AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

            if (fw->adapters[i].callbacks.encode_message) {
                void *encoded = NULL;
                size_t encoded_size = 0;
                int rc = fw->adapters[i].callbacks.encode_message(fw->adapters[i].adapter_context,
                                                                  message, &encoded, &encoded_size);
                if (rc != 0) {
                    AIRY_FREE(encoded);
                    fw->adapters[i].error_count++;
                    return rc;
                }

                if (encoded && encoded_size > 0 && fw->adapters[i].callbacks.handle_request) {
                    char params_json[64];
                    snprintf(params_json, sizeof(params_json), "{\"size\":%zu}", encoded_size);
                    char *response = NULL;
                    int send_rc =
                        fw->adapters[i].callbacks.handle_request(fw->adapters[i].adapter_context,
                                                                 "send", params_json, &response);
                    AIRY_FREE(response);
                    if (send_rc != 0) {
                        fw->adapters[i].error_count++;
                    }
                }

                AIRY_FREE(encoded);
            }

            fw->adapters[i].messages_processed++;
            fw->adapters[i].last_activity_ms = current_time_ms();
            fw->total_messages++;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_handle_request(proto_ext_framework_t *fw, const char *adapter_name,
                             const char *method, const char *params_json, char **response_json)
{
    if (!fw || !adapter_name || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_handle_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, adapter_name) == 0) {
            if (fw->adapters[i].state != PROTO_EXT_STATE_RUNNING)
                AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
            if (!fw->adapters[i].callbacks.handle_request)
                AIRY_ERROR(AIRY_ERR_OUT_OF_MEMORY, "out of memory");

            int rc = fw->adapters[i].callbacks.handle_request(fw->adapters[i].adapter_context,
                                                              method, params_json, response_json);

            fw->adapters[i].messages_processed++;
            fw->adapters[i].last_activity_ms = current_time_ms();
            fw->total_messages++;

            if (rc != 0)
                fw->adapters[i].error_count++;
            return rc;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_auto_route(proto_ext_framework_t *fw, const unified_message_t *message,
                         char **adapter_name)
{
    if (!fw || !message || !adapter_name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_auto_route: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (fw->adapters[i].state != PROTO_EXT_STATE_RUNNING)
            continue;
        if (fw->adapters[i].descriptor.protocol_type == message->protocol) {
            *adapter_name = AIRY_STRDUP(fw->adapters[i].descriptor.name);
            return 0;
        }
    }

    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (fw->adapters[i].state != PROTO_EXT_STATE_RUNNING)
            continue;
        if (fw->adapters[i].descriptor.protocol_type == PROTOCOL_CUSTOM) {
            *adapter_name = AIRY_STRDUP(fw->adapters[i].descriptor.name);
            return 0;
        }
    }

    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_negotiate(proto_ext_framework_t *fw, const char *adapter_name,
                        const char *client_version, char **agreed_version)
{
    if (!fw || !adapter_name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_negotiate: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, adapter_name) == 0) {
            if (!fw->adapters[i].callbacks.negotiate_version) {
                *agreed_version = AIRY_STRDUP(fw->adapters[i].descriptor.version);
                return 0;
            }
            return fw->adapters[i].callbacks.negotiate_version(fw->adapters[i].adapter_context,
                                                               client_version, agreed_version);
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}
