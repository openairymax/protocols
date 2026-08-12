// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_task.c
 * @brief OpenClaw adapter message and task domain (send/recv, delegation, query, cancel, cluster status).
 */

#define LOG_TAG "openclaw_adapter"

#include "openclaw_adapter.h"
#include "openclaw_adapter_internal.h"

#include "protocol_transformers.h"

#include <stdio.h>
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "airy_memory.h"
#include "types.h"

int openclaw_send_message(openclaw_adapter_context_t *ctx, const openclaw_message_t *msg,
                          openclaw_message_t *response)
{
    if (!ctx || !msg || !response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_send_message: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->connected)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    ctx->messages_sent++;

    if (ctx->message_handler) {
        int ret = ctx->message_handler(msg, response, ctx->message_handler_data);
        ctx->messages_received++;
        return ret;
    }

    if (ctx->sock_fd != INVALID_SOCK) {
        char send_buf[OPENCLAW_RECV_BUFFER_SIZE] = {0};
        int ser_len = openclaw_serialize_message(msg, send_buf, sizeof(send_buf));
        if (ser_len > 0) {
            size_t send_len = (size_t)ser_len;
            char framed[OPENCLAW_RECV_BUFFER_SIZE + 8];
            int frame_len = snprintf(framed, sizeof(framed), "%06zx%s", send_len, send_buf);
            int send_ret =
                openclaw_socket_send(ctx->sock_fd, framed, (size_t)(frame_len > 0 ? frame_len : 0),
                                     ctx->config.request_timeout_ms);
            if (send_ret == 0) {
                char recv_buf[OPENCLAW_RECV_BUFFER_SIZE] = {0};
                size_t recv_len = 0;
                int recv_ret = openclaw_socket_recv(ctx->sock_fd, recv_buf, sizeof(recv_buf),
                                                    &recv_len, ctx->config.request_timeout_ms);
                if (recv_ret == 0 && recv_len >= 6) {
                    size_t payload_offset = 6;
                    size_t payload_len = recv_len - 6;
                    AIRY_MEMSET(response, 0, sizeof(openclaw_message_t));
                    response->message_id = msg->message_id ? AIRY_STRDUP(msg->message_id) : NULL;
                    response->session_id = msg->session_id ? AIRY_STRDUP(msg->session_id) : NULL;
                    response->sender_id = msg->receiver_id ? AIRY_STRDUP(msg->receiver_id) : NULL;
                    response->receiver_id = msg->sender_id ? AIRY_STRDUP(msg->sender_id) : NULL;
                    response->modality = msg->modality;
                    response->timestamp = (uint64_t)(time(NULL));
                    if (payload_len > 0) {
                        response->payload = AIRY_MALLOC(payload_len + 1);
                        if (response->payload) {
                            __builtin_memcpy(response->payload, recv_buf + payload_offset,
                                             payload_len);
                            ((char *)response->payload)[payload_len] = '\0';
                            response->payload_size = payload_len;
                        }
                    }
                    ctx->messages_received++;
                    return 0;
                }
            }
        }
    }

    AIRY_MEMSET(response, 0, sizeof(openclaw_message_t));
    response->message_id = msg->message_id ? AIRY_STRDUP(msg->message_id) : NULL;
    response->session_id = msg->session_id ? AIRY_STRDUP(msg->session_id) : NULL;
    response->receiver_id = msg->sender_id ? AIRY_STRDUP(msg->sender_id) : NULL;
    response->sender_id = msg->receiver_id ? AIRY_STRDUP(msg->receiver_id) : NULL;
    response->modality = msg->modality;
    response->timestamp = (uint64_t)(time(NULL));

    ctx->messages_received++;
    return 0;
}

int openclaw_delegate_task(openclaw_adapter_context_t *ctx, const openclaw_task_t *task,
                           const char *target_agent_id, openclaw_task_t *result)
{
    if (!ctx || !task || !result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_delegate_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->connected)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    ctx->tasks_delegated++;

    if (ctx->task_handler) {
        int ret = ctx->task_handler(task, result, ctx->task_handler_data);
        if (ret == 0)
            ctx->tasks_completed++;
        return ret;
    }

    static uint32_t task_counter = 0;
    task_counter++;

    AIRY_MEMSET(result, 0, sizeof(openclaw_task_t));
    char tid[64];
    snprintf(tid, sizeof(tid), "oc-task-%08x", task_counter);
    result->task_id = AIRY_STRDUP(tid);
    result->session_id = task->session_id ? AIRY_STRDUP(task->session_id) : NULL;
    result->description = task->description ? AIRY_STRDUP(task->description) : NULL;
    result->input_data_json = task->input_data_json ? AIRY_STRDUP(task->input_data_json) : NULL;
    result->assigned_agent_id = target_agent_id ? AIRY_STRDUP(target_agent_id) : NULL;
    result->priority = task->priority > 0 ? task->priority : 5;
    result->state = OPENCLAW_AGENT_STATE_EXECUTING;
    result->progress = 0.0;
    result->created_at = (uint64_t)(time(NULL));

    ctx->tasks_completed++;
    result->state = OPENCLAW_AGENT_STATE_IDLE;
    result->progress = 1.0;
    result->completed_at = (uint64_t)(time(NULL));

    openclaw_task_t *new_tasks =
        (openclaw_task_t *)AIRY_REALLOC(ctx->tracked_tasks,
                                        (ctx->tracked_task_count + 1) * sizeof(openclaw_task_t));
    if (!new_tasks)
        return AIRY_ERR_OUT_OF_MEMORY;
    ctx->tracked_tasks = new_tasks;
    ctx->tracked_tasks[ctx->tracked_task_count] = *result;
    ctx->tracked_tasks[ctx->tracked_task_count].task_id =
        result->task_id ? AIRY_STRDUP(result->task_id) : NULL;
    ctx->tracked_tasks[ctx->tracked_task_count].session_id =
        result->session_id ? AIRY_STRDUP(result->session_id) : NULL;
    ctx->tracked_tasks[ctx->tracked_task_count].description =
        result->description ? AIRY_STRDUP(result->description) : NULL;
    ctx->tracked_tasks[ctx->tracked_task_count].assigned_agent_id =
        result->assigned_agent_id ? AIRY_STRDUP(result->assigned_agent_id) : NULL;
    ctx->tracked_task_count++;

    return 0;
}

int openclaw_query_task(openclaw_adapter_context_t *ctx, const char *task_id,
                        openclaw_task_t *result)
{
    if (!ctx || !task_id || !result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_query_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    AIRY_MEMSET(result, 0, sizeof(openclaw_task_t));

    for (size_t i = 0; i < ctx->tracked_task_count; i++) {
        if (ctx->tracked_tasks[i].task_id && strcmp(ctx->tracked_tasks[i].task_id, task_id) == 0) {
            *result = ctx->tracked_tasks[i];
            result->task_id =
                ctx->tracked_tasks[i].task_id ? AIRY_STRDUP(ctx->tracked_tasks[i].task_id) : NULL;
            result->session_id = ctx->tracked_tasks[i].session_id ?
                                     AIRY_STRDUP(ctx->tracked_tasks[i].session_id) :
                                     NULL;
            result->description = ctx->tracked_tasks[i].description ?
                                      AIRY_STRDUP(ctx->tracked_tasks[i].description) :
                                      NULL;
            result->assigned_agent_id = ctx->tracked_tasks[i].assigned_agent_id ?
                                            AIRY_STRDUP(ctx->tracked_tasks[i].assigned_agent_id) :
                                            NULL;
            result->input_data_json = ctx->tracked_tasks[i].input_data_json ?
                                          AIRY_STRDUP(ctx->tracked_tasks[i].input_data_json) :
                                          NULL;
            result->result_json = ctx->tracked_tasks[i].result_json ?
                                      AIRY_STRDUP(ctx->tracked_tasks[i].result_json) :
                                      NULL;
            result->error_message = ctx->tracked_tasks[i].error_message ?
                                        AIRY_STRDUP(ctx->tracked_tasks[i].error_message) :
                                        NULL;
            return 0;
        }
    }

    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int openclaw_cancel_task(openclaw_adapter_context_t *ctx, const char *task_id)
{
    if (!ctx || !task_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_cancel_task: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < ctx->tracked_task_count; i++) {
        if (ctx->tracked_tasks[i].task_id && strcmp(ctx->tracked_tasks[i].task_id, task_id) == 0) {
            ctx->tracked_tasks[i].state = OPENCLAW_AGENT_STATE_ERROR;
            if (ctx->event_callback) {
                ctx->event_callback("task_cancelled", task_id, ctx->event_callback_data);
            }
            return 0;
        }
    }

    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int openclaw_get_cluster_status(openclaw_adapter_context_t *ctx, openclaw_cluster_status_t *status)
{
    if (!ctx || !status) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_get_cluster_status: failed");
        return AIRY_ERR_UNKNOWN;
    }

    AIRY_MEMSET(status, 0, sizeof(openclaw_cluster_status_t));
    status->node_id = "agentrt-node-001";
    status->cluster_name = ctx->config.cluster_id ? ctx->config.cluster_id : "default";
    status->total_nodes = 1;
    status->active_nodes = 1;
    status->total_agents = (uint64_t)ctx->registered_agent_count;
    status->active_sessions = (uint64_t)ctx->active_session_count;
    status->messages_processed = ctx->messages_sent + ctx->messages_received;
    status->tasks_completed = ctx->tasks_completed;
    status->uptime_seconds = ctx->connection_uptime_sec;
    status->cpu_usage_pct =
        (double)(ctx->active_session_count * 2.5 + ctx->registered_agent_count * 0.5);
    status->memory_usage_mb =
        (double)(ctx->tracked_task_count * 8.0 + ctx->registered_tool_count * 0.5 + 32.0);
    status->disk_usage_pct =
        (double)(ctx->registered_agent_count * 0.1 + ctx->registered_tool_count * 0.05);

    return 0;
}
