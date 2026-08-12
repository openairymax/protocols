// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter.c
 * @brief OpenClaw Platform Integration Adapter Implementation
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

static openclaw_adapter_context_t *g_openclaw_instance = NULL;

openclaw_config_t openclaw_config_default(void)
{
    openclaw_config_t cfg = {0};
    cfg.endpoint_url = "http://localhost:28080";
    cfg.api_key = NULL;
    cfg.organization_id = "default";
    cfg.cluster_id = "default";
    cfg.mode = OPENCLAW_MODE_STANDALONE;
    cfg.default_security_level = OPENCLAW_SECURITY_LEVEL_INTERNAL;
    cfg.heartbeat_interval_sec = OPENCLAW_HEARTBEAT_INTERVAL_SEC;
    cfg.request_timeout_ms = OPENCLAW_DEFAULT_TIMEOUT_MS;
    cfg.max_sessions = OPENCLAW_MAX_SESSIONS;
    cfg.max_context_kb = OPENCLAW_MAX_CONTEXT_KB;
    cfg.enable_multimodal = true;
    cfg.enable_tool_sharing = true;
    cfg.enable_audit_log = true;
    cfg.enable_metrics = true;
    cfg.custom_headers_json = NULL;
    cfg.reconnect_max_attempts = 5;
    cfg.reconnect_delay_ms = 2000;
    return cfg;
}

openclaw_adapter_context_t *openclaw_adapter_create(const openclaw_config_t *config)
{
    if (!config)
        return NULL;

    openclaw_adapter_context_t *ctx =
        (openclaw_adapter_context_t *)AIRY_CALLOC(1, sizeof(openclaw_adapter_context_t));
    if (!ctx)
        return NULL;

    __builtin_memcpy(&ctx->config, config, sizeof(openclaw_config_t));

    if (config->endpoint_url)
        ctx->config.endpoint_url = AIRY_STRDUP(config->endpoint_url);
    if (config->api_key)
        ctx->config.api_key = AIRY_STRDUP(config->api_key);
    if (config->organization_id)
        ctx->config.organization_id = AIRY_STRDUP(config->organization_id);
    if (config->cluster_id)
        ctx->config.cluster_id = AIRY_STRDUP(config->cluster_id);
    if (config->custom_headers_json)
        ctx->config.custom_headers_json = AIRY_STRDUP(config->custom_headers_json);

    ctx->initialized = true;
    ctx->connected = false;
    ctx->sock_fd = INVALID_SOCK;
    ctx->registered_agents = NULL;
    ctx->registered_agent_count = 0;
    ctx->active_sessions = NULL;
    ctx->active_session_count = 0;
    ctx->messages_sent = 0;
    ctx->messages_received = 0;
    ctx->tasks_delegated = 0;
    ctx->tasks_completed = 0;
    ctx->connection_uptime_sec = 0;

    g_openclaw_instance = ctx;
    return ctx;
}

void openclaw_adapter_destroy(openclaw_adapter_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx == g_openclaw_instance)
        g_openclaw_instance = NULL;

    if (ctx->connected)
        openclaw_disconnect(ctx);

    AIRY_FREE(ctx->config.endpoint_url);
    AIRY_FREE(ctx->config.api_key);
    AIRY_FREE(ctx->config.organization_id);
    AIRY_FREE(ctx->config.cluster_id);
    AIRY_FREE(ctx->config.custom_headers_json);

    for (size_t i = 0; i < ctx->registered_agent_count; i++)
        openclaw_agent_card_destroy(&ctx->registered_agents[i]);
    AIRY_FREE(ctx->registered_agents);

    for (size_t i = 0; i < ctx->active_session_count; i++)
        openclaw_session_destroy(&ctx->active_sessions[i]);
    AIRY_FREE(ctx->active_sessions);

    for (size_t i = 0; i < ctx->registered_tool_count; i++)
        openclaw_tool_info_destroy(&ctx->registered_tools[i]);
    AIRY_FREE(ctx->registered_tools);

    for (size_t i = 0; i < ctx->tracked_task_count; i++)
        openclaw_task_destroy(&ctx->tracked_tasks[i]);
    AIRY_FREE(ctx->tracked_tasks);

    AIRY_FREE(ctx->connected_endpoint);
    AIRY_FREE(ctx->send_buffer);

    AIRY_MEMSET(ctx, 0, sizeof(openclaw_adapter_context_t));
    AIRY_FREE(ctx);
}

bool openclaw_adapter_is_initialized(const openclaw_adapter_context_t *ctx)
{
    return ctx && ctx->initialized;
}

const char *openclaw_adapter_version(void)
{
    return OPENCLAW_ADAPTER_VERSION;
}

const char *openclaw_adapter_platform_version(void)
{
    return OPENCLAW_PLATFORM_VERSION;
}

int openclaw_connect(openclaw_adapter_context_t *ctx)
{
    if (!ctx || !ctx->initialized) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_connect: not initialized");
        return AIRY_ERR_UNKNOWN;
    }
    if (ctx->connected)
        return 0;

    char host[256] = {0};
    int port = 28080;

    openclaw_parse_endpoint(ctx->config.endpoint_url, host, sizeof(host), &port);

    socket_fd_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "Failed to create socket: errno=%d",
                 sock_errno);
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    }

    int ret = openclaw_socket_connect(fd, host, port, ctx->config.request_timeout_ms);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "Connect failed: %.80s:%d (ret=%d)",
                 host, port, ret);
        close_socket(fd);
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
    }

    ctx->sock_fd = fd;
    ctx->connected = true;
    ctx->connection_uptime_sec = 0;
    ctx->connect_timestamp = (uint64_t)time(NULL);
    ctx->last_error[0] = '\0';
    return 0;
}

int openclaw_disconnect(openclaw_adapter_context_t *ctx)
{
    if (!ctx || !ctx->connected) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_disconnect: IO error");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < ctx->active_session_count; i++) {
        if (ctx->active_sessions[i].is_active)
            ctx->active_sessions[i].is_active = false;
    }

    if (ctx->sock_fd != INVALID_SOCK) {
        close_socket(ctx->sock_fd);
        ctx->sock_fd = INVALID_SOCK;
    }

    ctx->connected = false;
    return 0;
}

bool openclaw_is_connected(const openclaw_adapter_context_t *ctx)
{
    return ctx && ctx->connected;
}

static proto_adapter_t g_openclaw_adapter = {0};
static pthread_once_t g_openclaw_adapter_once = PTHREAD_ONCE_INIT;

static void openclaw_adapter_init_once(void)
{
    g_openclaw_adapter.name = "OpenClaw";
    g_openclaw_adapter.version = OPENCLAW_ADAPTER_VERSION;
    g_openclaw_adapter.description = "OpenClaw Platform Integration Adapter - offline private AI "
                                     "Agent platform with multimodal capabilities";
    g_openclaw_adapter.type = AIRY_PROTOCOL_OPENCLAW;
    g_openclaw_adapter.init = openclaw_proto_init;
    g_openclaw_adapter.destroy = openclaw_proto_destroy;
    g_openclaw_adapter.encode = openclaw_proto_encode;
    g_openclaw_adapter.decode = openclaw_proto_decode;
    g_openclaw_adapter.connect = openclaw_proto_connect;
    g_openclaw_adapter.disconnect = openclaw_proto_disconnect;
    g_openclaw_adapter.is_connected = openclaw_proto_is_connected;
    g_openclaw_adapter.send = openclaw_proto_send;
    g_openclaw_adapter.receive = openclaw_proto_receive;
    g_openclaw_adapter.handle_request = openclaw_proto_handle_request;
    g_openclaw_adapter.get_version = openclaw_proto_get_version;
    g_openclaw_adapter.capabilities = openclaw_proto_capabilities;
    g_openclaw_adapter.get_stats = openclaw_proto_get_stats;
}

const proto_adapter_t *openclaw_get_protocol_adapter(void)
{
    pthread_once(&g_openclaw_adapter_once, openclaw_adapter_init_once);
    return &g_openclaw_adapter;
}

void openclaw_agent_card_destroy(openclaw_agent_card_t *card)
{
    if (!card)
        return;
    AIRY_FREE(card->agent_id);
    AIRY_FREE(card->name);
    AIRY_FREE(card->description);
    AIRY_FREE(card->version);
    AIRY_MEMSET(card, 0, sizeof(openclaw_agent_card_t));
}

void openclaw_tool_info_destroy(openclaw_tool_info_t *tool)
{
    if (!tool)
        return;
    AIRY_FREE(tool->tool_id);
    AIRY_FREE(tool->name);
    AIRY_FREE(tool->description);
    AIRY_FREE(tool->input_schema_json);
    AIRY_FREE(tool->output_schema_json);
    AIRY_MEMSET(tool, 0, sizeof(openclaw_tool_info_t));
}

void openclaw_session_destroy(openclaw_session_t *session)
{
    if (!session)
        return;
    AIRY_FREE(session->session_id);
    AIRY_FREE(session->agent_id);
    AIRY_FREE(session->parent_session_id);
    AIRY_MEMSET(session, 0, sizeof(openclaw_session_t));
}

void openclaw_message_destroy(openclaw_message_t *msg)
{
    if (!msg)
        return;
    AIRY_FREE(msg->message_id);
    AIRY_FREE(msg->session_id);
    AIRY_FREE(msg->sender_id);
    AIRY_FREE(msg->receiver_id);
    AIRY_FREE(msg->content_type);
    AIRY_FREE(msg->payload);
    AIRY_MEMSET(msg, 0, sizeof(openclaw_message_t));
}

void openclaw_task_destroy(openclaw_task_t *task)
{
    if (!task)
        return;
    AIRY_FREE(task->task_id);
    AIRY_FREE(task->session_id);
    AIRY_FREE(task->description);
    AIRY_FREE(task->input_data_json);
    AIRY_FREE(task->assigned_agent_id);
    AIRY_FREE(task->result_json);
    AIRY_FREE(task->error_message);
    AIRY_MEMSET(task, 0, sizeof(openclaw_task_t));
}

void openclaw_cluster_status_destroy(openclaw_cluster_status_t *status)
{
    if (!status)
        return;
    AIRY_FREE(status->node_id);
    AIRY_FREE(status->cluster_name);
    AIRY_MEMSET(status, 0, sizeof(openclaw_cluster_status_t));
}
