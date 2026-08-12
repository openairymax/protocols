// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_monitor.c
 * @brief OpenClaw adapter monitoring and callback domain (handler registration/heartbeat/statistics).
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

int openclaw_set_message_handler(openclaw_adapter_context_t *ctx,
                                 openclaw_message_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_set_message_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->message_handler = handler;
    ctx->message_handler_data = user_data;
    return 0;
}

int openclaw_set_task_handler(openclaw_adapter_context_t *ctx, openclaw_task_handler_t handler,
                              void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_set_task_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->task_handler = handler;
    ctx->task_handler_data = user_data;
    return 0;
}

int openclaw_set_event_callback(openclaw_adapter_context_t *ctx, openclaw_event_callback_t callback,
                                void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_set_event_callback: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->event_callback = callback;
    ctx->event_callback_data = user_data;
    return 0;
}

int openclaw_set_status_callback(openclaw_adapter_context_t *ctx,
                                 openclaw_status_callback_t callback, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_set_status_callback: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->status_callback = callback;
    ctx->status_callback_data = user_data;
    return 0;
}

int openclaw_send_heartbeat(openclaw_adapter_context_t *ctx)
{
    if (!ctx || !ctx->connected) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_send_heartbeat: IO error");
        return AIRY_ERR_UNKNOWN;
    }

    ctx->connection_uptime_sec += ctx->config.heartbeat_interval_sec;

    for (size_t i = 0; i < ctx->registered_agent_count; i++) {
        if (ctx->registered_agents[i].is_active) {
            ctx->registered_agents[i].last_heartbeat = (uint64_t)(time(NULL));
        }
    }

    if (ctx->status_callback) {
        openclaw_cluster_status_t status;
        openclaw_get_cluster_status(ctx, &status);
        ctx->status_callback(&status, ctx->status_callback_data);
    }

    return 0;
}

int openclaw_get_statistics(openclaw_adapter_context_t *ctx, char *stats_json, size_t buffer_size)
{
    if (!ctx || !stats_json || buffer_size < 64) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_get_statistics: failed");
        return AIRY_ERR_UNKNOWN;
    }

    openclaw_cluster_status_t status;
    openclaw_get_cluster_status(ctx, &status);

    int written = snprintf(
        stats_json, buffer_size,
        "{"
        "\"adapter_version\":\"%s\","
        "\"platform_version\":\"%s\","
        "\"mode\":\"%s\","
        "\"connected\":%s,"
        "\"registered_agents\":%zu,"
        "\"active_sessions\":%zu,"
        "\"messages_sent\":%llu,"
        "\"messages_received\":%llu,"
        "\"tasks_delegated\":%llu,"
        "\"tasks_completed\":%llu,"
        "\"uptime_seconds\":%llu,"
        "\"cluster\":{"
        "\"node_id\":\"%s\","
        "\"cluster_name\":\"%s\","
        "\"total_agents\":%llu,"
        "\"active_sessions\":%llu"
        "}"
        "}",
        OPENCLAW_ADAPTER_VERSION, OPENCLAW_PLATFORM_VERSION,
        ctx->config.mode == OPENCLAW_MODE_STANDALONE ? "standalone" :
        ctx->config.mode == OPENCLAW_MODE_CLUSTERED  ? "clustered" :
        ctx->config.mode == OPENCLAW_MODE_HYBRID     ? "hybrid" :
                                                       "embedded",
        ctx->connected ? "true" : "false", ctx->registered_agent_count, ctx->active_session_count,
        (unsigned long long)ctx->messages_sent, (unsigned long long)ctx->messages_received,
        (unsigned long long)ctx->tasks_delegated, (unsigned long long)ctx->tasks_completed,
        (unsigned long long)ctx->connection_uptime_sec, status.node_id ? status.node_id : "",
        status.cluster_name ? status.cluster_name : "", (unsigned long long)status.total_agents,
        (unsigned long long)status.active_sessions);

    openclaw_cluster_status_destroy(&status);

    return (written >= 0 && (size_t)written < buffer_size) ? 0 : -2;
}
