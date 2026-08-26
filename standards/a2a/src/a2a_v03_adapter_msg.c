// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_msg.c
 * @brief A2A v0.3 message exchange and event push domain (message/negotiation/notification/streaming/handler registration).
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <string.h>

int a2a_v03_send_message(a2a_v03_context_t *ctx, const char *target_agent_id,
                         const a2a_message_t *message, a2a_message_t **response,
                         size_t *response_count)
{
    if (!ctx || !target_agent_id || !message) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_send_message: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (adapter->message_handler) {
        return adapter->message_handler(ctx, target_agent_id, message, response, response_count,
                                        adapter->message_handler_user_data);
    }

    if (response && response_count) {
        a2a_message_t *resp = (a2a_message_t *)AIRY_CALLOC(1, sizeof(a2a_message_t));
        if (resp) {
            resp->role = AIRY_STRDUP("assistant");
            resp->type = A2A_MSG_STRUCTURED;
            resp->content_json = AIRY_STRDUP("{\"status\":\"received\",\"ack\":true}");
            resp->mime_type = AIRY_STRDUP("application/json");
            *response = resp;
            *response_count = 1;
        }
    }
    return 0;
}

int a2a_v03_negotiate(a2a_v03_context_t *ctx, const a2a_negotiation_t *proposal,
                      a2a_negotiation_action_t *response_action, char **response_terms)
{
    if (!ctx || !proposal) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_negotiate: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (adapter->negotiation_handler) {
        return adapter->negotiation_handler(ctx, proposal, response_action, response_terms,
                                            adapter->negotiation_handler_user_data);
    }

    if (response_action)
        *response_action = A2A_NEGOTIATE_REJECT;
    if (response_terms)
        *response_terms = NULL;
    return 0;
}

int a2a_v03_subscribe_notifications(a2a_v03_context_t *ctx, a2a_notification_handler_t handler,
                                    void *user_data)
{
    if (!ctx || !handler) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_subscribe_notifications: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->notification_handler = handler;
    adapter->notification_handler_user_data = user_data;
    return 0;
}

int a2a_v03_unsubscribe_notifications(a2a_v03_context_t *ctx)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_unsubscribe_notifications: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->notification_handler = NULL;
    adapter->notification_handler_user_data = NULL;
    return 0;
}

int a2a_v03_send_notification(a2a_v03_context_t *ctx, const a2a_notification_t *notification)
{
    if (!ctx || !notification) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_send_notification: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->notification_handler) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__, "operation failed");
        return AIRY_ERR_NULL_POINTER;
    }
    adapter->notification_handler(ctx, notification, adapter->notification_handler_user_data);
    return 0;
}

int a2a_v03_stream_task_update(a2a_v03_context_t *ctx, const char *task_id, double progress,
                               const char *chunk_json, bool is_final)
{
    if (!ctx || !task_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_stream_task_update: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    for (size_t i = 0; i < adapter->task_count; i++) {
        if (strcmp(adapter->tasks[i]->id, task_id) == 0) {
            adapter->tasks[i]->progress = progress;
            adapter->tasks[i]->updated_at = a2a_timestamp_ms();
            if (is_final) {
                adapter->tasks[i]->state = A2A_TASK_COMPLETED;
                if (chunk_json) {
                    AIRY_FREE(adapter->tasks[i]->output_json);
                    adapter->tasks[i]->output_json = AIRY_STRDUP(chunk_json);
                }
            }
            break;
        }
    }

    if (adapter->streaming_handler) {
        adapter->streaming_handler(ctx, task_id, progress, chunk_json, is_final,
                                   adapter->streaming_handler_user_data);
    }
    return 0;
}

int a2a_v03_set_task_handler(a2a_v03_context_t *ctx, a2a_task_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_task_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->task_handler = handler;
    adapter->task_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_message_handler(a2a_v03_context_t *ctx, a2a_message_handler_t handler,
                                void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_message_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->message_handler = handler;
    adapter->message_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_negotiation_handler(a2a_v03_context_t *ctx, a2a_negotiation_handler_t handler,
                                    void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_negotiation_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->negotiation_handler = handler;
    adapter->negotiation_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_streaming_handler(a2a_v03_context_t *ctx, a2a_streaming_handler_t handler,
                                  void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_streaming_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->streaming_handler = handler;
    adapter->streaming_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_transport(a2a_v03_context_t *ctx, int (*write_fn)(void *, const void *, size_t),
                          void *transport_ctx)
{
    if (!ctx || !write_fn) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_transport: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->transport_write = write_fn;
    adapter->transport_ctx = transport_ctx;
    return 0;
}

int a2a_v03_set_transport_read(a2a_v03_context_t *ctx,
                               int (*read_fn)(void *, void **, size_t *, uint32_t),
                               void *transport_ctx)
{
    if (!ctx || !read_fn) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_transport_read: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->transport_read = read_fn;
    adapter->transport_ctx = transport_ctx;
    return 0;
}
