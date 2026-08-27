// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter.c
 * @brief OpenJiuwen protocol adapter 门面/生命周期域：创建/销毁、
 *        默认配置、连接校验、能力查询与全局接口实例（网络传输见
 *        openjiuwen_adapter_net.c，连接收发 ops 见 openjiuwen_adapter_io.c，
 *        消息转换见 openjiuwen_adapter_msg.c）。
 *
 * Implements a protocol compatibility layer with the OpenJiuwen platform,
 * supporting message format conversion and interoperability.
 */

#include "openjiuwen_adapter.h"
#include "openjiuwen_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"
#include "platform.h"
#include "safe_string_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============ 工具：消息 ID/时间戳 ============ */

uint32_t openjiuwen_generate_message_id(void)
{
    static uint32_t counter = 0;
    return ++counter;
}

uint32_t openjiuwen_get_timestamp(void)
{
    return (uint32_t)time(NULL);
}

uint64_t openjiuwen_get_timestamp_ms(void)
{
    return airy_time_ms();
}

/* ============================================================================

 * ============================================================================ */

int openjiuwen_adapter_init(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;
    if (adapter->initialized)
        return 0;
    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    adapter->consecutive_errors = 0;
    adapter->message_counter = 0;
    adapter->total_reconnects = 0;
    adapter->last_heartbeat_sec = 0;
    adapter->last_error_code = 0;
    adapter->last_activity_ms = 0;
    adapter->initialized = true;
    AIRY_LOG_INFO("OpenJiuwen adapter initialized");
    return 0;
}

__attribute__((unused)) int openjiuwen_adapter_deinit(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;

    openjiuwen_net_disconnect(adapter);
    adapter->consecutive_errors = 0;
    AIRY_LOG_INFO("OpenJiuwen: deinitialized");
    return 0;
}

int openjiuwen_adapter_is_connected(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return 0;
    return adapter->conn_state == OPENJIUWEN_CONN_CONNECTED ? 1 : 0;
}

int openjiuwen_adapter_handle_request(void *context, const void *req, void **resp)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !req || !resp)
        return AIRY_ERR_NULL_POINTER;
    if (!adapter->initialized)
        return AIRY_ERR_SYS_NOT_INIT;
    if (adapter->conn_state != OPENJIUWEN_CONN_CONNECTED)
        return AIRY_ERR_INVALID_PARAM;

    const unified_message_t *request = (const unified_message_t *)req;
    unified_message_t *response = (unified_message_t *)AIRY_CALLOC(1, sizeof(unified_message_t));
    if (!response)
        return AIRY_ERR_OUT_OF_MEMORY;

    response->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    response->message_id = openjiuwen_generate_message_id();
    response->timestamp = openjiuwen_get_timestamp();
    safe_strcpy(response->source_agent, "OpenJiuwen", sizeof(response->source_agent));
    safe_strcpy(response->target_agent, request->source_agent, sizeof(response->target_agent));
    response->payload = NULL;
    response->payload_size = 0;

    adapter->message_counter++;
    adapter->last_activity_ms = openjiuwen_get_timestamp_ms();
    *resp = response;
    return 0;
}

int openjiuwen_adapter_get_version(void *context, char *version_buf, size_t max_size)
{
    if (!version_buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    const char *ver = OPENJIUWEN_PROTOCOL_VERSION;
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(version_buf, ver, len);
    version_buf[len] = '\0';
    return 0;
}

uint32_t openjiuwen_adapter_capabilities(void *context)
{
    return 0x0F;
}

int openjiuwen_adapter_get_stats(void *context, char *stats_json, size_t max_size)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !stats_json || max_size == 0)
        return AIRY_ERR_NULL_POINTER;

    int written =
        snprintf(stats_json, max_size,
                 "{\"messages_sent\":%u,\"consecutive_errors\":%u,"
                 "\"total_reconnects\":%u,\"conn_state\":%d,"
                 "\"last_error_code\":%u}",
                 adapter->message_counter, adapter->consecutive_errors, adapter->total_reconnects,
                 adapter->conn_state, adapter->last_error_code);

    return (written > 0 && (size_t)written < max_size) ? 0 : -2;
}

/**
  * @brief Destroy an adapter instance
 */
int openjiuwen_destroy(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter) {
        return 0;
    }

    if (adapter->connection_handle) {
        openjiuwen_net_disconnect(adapter);
    }

    adapter->initialized = false;
    adapter->consecutive_errors = 0;
    adapter->message_counter = 0;

    AIRY_LOG_INFO("OpenJiuwen adapter destroyed (reconnects=%u, last_error=%u)",
             adapter->total_reconnects, adapter->last_error_code);
    return 0;
}

/* ============================================================================

 * ============================================================================ */

void openjiuwen_get_default_config(openjiuwen_config_t *config)
{
    if (!config)
        return;

    AIRY_MEMSET(config, 0, sizeof(openjiuwen_config_t));

    safe_strcpy(config->endpoint, "http://localhost:8080", sizeof(config->endpoint));
    config->timeout_ms = OPENJIUWEN_TIMEOUT_MS;
    config->enable_compression = false;
    config->enable_encryption = false;
    config->max_retries = 3;
}

const protocol_adapter_t *openjiuwen_adapter_create(const openjiuwen_config_t *config)
{

    openjiuwen_adapter_t *adapter =
        (openjiuwen_adapter_t *)AIRY_CALLOC(1, sizeof(openjiuwen_adapter_t));
    if (!adapter) {
        AIRY_LOG_ERROR("Failed to allocate OpenJiuwen adapter");
        return NULL;
    }

    if (config) {
        __builtin_memcpy(&adapter->config, config, sizeof(openjiuwen_config_t));
    } else {
        openjiuwen_get_default_config(&adapter->config);
    }

    adapter->base.type = AIRY_PROTOCOL_OPENJIUWEN;
    adapter->base.name = "OpenJiuwen Protocol Adapter";
    adapter->base.version = OPENJIUWEN_PROTOCOL_VERSION;
    adapter->base.description = "OpenJiuwen platform protocol adapter";

    adapter->base.context = adapter;
    adapter->base.send = openjiuwen_send_message;
    adapter->base.receive = openjiuwen_receive_message;
    adapter->base.destroy = openjiuwen_destroy;

    adapter->initialized = true;
    adapter->message_counter = 0;
    adapter->connection_handle = NULL;
    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    adapter->consecutive_errors = 0;
    adapter->total_reconnects = 0;
    adapter->last_heartbeat_sec = 0;
    adapter->last_error_code = 0;
    adapter->last_activity_ms = 0;

    AIRY_LOG_INFO("OpenJiuwen adapter created successfully (endpoint=%s)", adapter->config.endpoint);

    return &adapter->base;
}

int openjiuwen_verify_connection(const protocol_adapter_t *adapter)
{
    if (!adapter || adapter->type != AIRY_PROTOCOL_OPENJIUWEN) {
        return AIRY_ERR_NULL_POINTER;
    }

    openjiuwen_adapter_t *impl = (openjiuwen_adapter_t *)adapter->context;
    if (!impl || !impl->initialized) {
        return AIRY_ERR_SYS_NOT_INIT;
    }

    if (impl->consecutive_errors >= OPENJIUWEN_MAX_CONSECUTIVE_ERRORS) {
        impl->conn_state = OPENJIUWEN_CONN_ERROR;
        AIRY_LOG_WARN("OpenJiuwen: connection verification failed - too many errors (%u)",
                 impl->consecutive_errors);
        return AIRY_ERR_IO;
    }

    /* A live connection that has been idle too long must be re-established. */
    uint32_t now = openjiuwen_get_timestamp();
    if (impl->conn_state == OPENJIUWEN_CONN_CONNECTED && impl->connection_handle &&
        now - impl->last_heartbeat_sec > OPENJIUWEN_HEARTBEAT_INTERVAL_SEC * 3) {
        AIRY_LOG_WARN("OpenJiuwen: connection stale (idle=%us), reconnecting", now - impl->last_heartbeat_sec);
        openjiuwen_net_disconnect(impl);
    }

    if (!impl->connection_handle) {
        int rc = openjiuwen_net_connect(impl);
        if (rc != 0)
            return rc;
    }

    impl->conn_state = OPENJIUWEN_CONN_CONNECTED;
    impl->last_heartbeat_sec = now;
    return 0;
}

int openjiuwen_get_capabilities(const protocol_adapter_t *adapter, char *capabilities,
                                size_t max_len)
{
    if (!adapter || !capabilities || max_len == 0) {
        return AIRY_ERR_NULL_POINTER;
    }

    const char *caps = "{"
                       "\"version\":\"" OPENJIUWEN_PROTOCOL_VERSION "\","
                       "\"features\":["
                       "\"agent_discovery\","
                       "\"task_delegation\","
                       "\"message_routing\","
                       "\"status_reporting\""
                       "],"
                       "\"supported_messages\":["
                       "\"request\","
                       "\"response\","
                       "\"notification\","
                       "\"heartbeat\","
                       "\"error\""
                       "]"
                       "}";

    safe_strcpy(capabilities, caps, max_len);

    return 0;
}

/* ============================================================================

 * ============================================================================ */

/*
  * Note: this global must be initialized via openjiuwen_adapter_create() before first use.
  * Only the declaration is provided; the instance is created at runtime.
 *
  * Usage example:
 *   const protocol_adapter_t* adapter = openjiuwen_adapter_create(NULL);
 *   unified_protocol_register_adapter(stack, adapter);
 */

static openjiuwen_adapter_t g_default_instance = {
    .base = {.type = AIRY_PROTOCOL_OPENJIUWEN,
             .name = "OpenJiuwen Protocol Adapter",
             .version = OPENJIUWEN_PROTOCOL_VERSION,
             .description = "OpenJiuwen platform protocol adapter",
             .context = NULL,
             .user_data = NULL,
             .init = openjiuwen_adapter_init,
             .destroy = openjiuwen_destroy,
             .encode = openjiuwen_adapter_encode,
             .decode = openjiuwen_adapter_decode,
             .connect = openjiuwen_adapter_connect,
             .disconnect = openjiuwen_adapter_disconnect,
             .is_connected = openjiuwen_adapter_is_connected,
             .send = openjiuwen_send_message,
             .receive = openjiuwen_receive_message,
             .handle_request = openjiuwen_adapter_handle_request,
             .get_version = openjiuwen_adapter_get_version,
             .capabilities = openjiuwen_adapter_capabilities,
             .get_stats = openjiuwen_adapter_get_stats},
    .config = {{0}, {0}, 0, false, false, 0},
    .connection_handle = NULL,
    .initialized = false,
    .message_counter = 0,
    .user_data = NULL,
    .conn_state = OPENJIUWEN_CONN_DISCONNECTED,
    .consecutive_errors = 0,
    .total_reconnects = 0,
    .last_heartbeat_sec = 0,
    .last_error_code = 0,
    .last_activity_ms = 0};

const protocol_adapter_t openjiuwen_adapter_interface = {
    .type = AIRY_PROTOCOL_OPENJIUWEN,
    .name = "OpenJiuwen Protocol Adapter",
    .version = OPENJIUWEN_PROTOCOL_VERSION,
    .description = "OpenJiuwen platform protocol adapter",
    .context = &g_default_instance,
    .user_data = NULL,
    .init = openjiuwen_adapter_init,
    .destroy = openjiuwen_destroy,
    .encode = openjiuwen_adapter_encode,
    .decode = openjiuwen_adapter_decode,
    .connect = openjiuwen_adapter_connect,
    .disconnect = openjiuwen_adapter_disconnect,
    .is_connected = openjiuwen_adapter_is_connected,
    .send = openjiuwen_send_message,
    .receive = openjiuwen_receive_message,
    .handle_request = openjiuwen_adapter_handle_request,
    .get_version = openjiuwen_adapter_get_version,
    .capabilities = openjiuwen_adapter_capabilities,
    .get_stats = openjiuwen_adapter_get_stats};
