// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter.c
 * @brief OpenJiuwen protocol adapter implementation.
 *
 * Implements a protocol compatibility layer with the OpenJiuwen platform,
 * supporting message format conversion and interoperability.
 */

#include "openjiuwen_adapter.h"

#include "error.h"
#include "logging.h"
#include "safe_string_utils.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "airy_memory.h"
#include "platform.h"

#include <stdio.h>
#include <time.h>

/* ============================================================================

 * ============================================================================ */

static uint32_t generate_message_id(void)
{
    static uint32_t counter = 0;
    return ++counter;
}

static uint32_t get_timestamp(void)
{
    return (uint32_t)time(NULL);
}

static uint64_t get_timestamp_ms(void)
{
    return airy_time_ms();
}

static int openjiuwen_reconnect(openjiuwen_adapter_t *adapter)
{
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;

    adapter->conn_state = OPENJIUWEN_CONN_RECONNECTING;
    adapter->total_reconnects++;

    uint32_t attempt = 0;
    uint32_t max_attempts = adapter->config.max_retries > 0 ? adapter->config.max_retries : 3;

    while (attempt < max_attempts) {
        uint32_t delay = OPENJIUWEN_RECONNECT_BASE_DELAY_MS << attempt;
        if (delay > OPENJIUWEN_RECONNECT_MAX_DELAY_MS)
            delay = OPENJIUWEN_RECONNECT_MAX_DELAY_MS;

        AIRY_LOG_WARN("OpenJiuwen: reconnect attempt %u/%u, waiting %ums", attempt + 1, max_attempts,
                 delay);

#ifdef _WIN32
        Sleep(delay);
#else
        struct timespec ts = {.tv_sec = delay / 1000, .tv_nsec = (delay % 1000) * 1000000LL};
        nanosleep(&ts, NULL);
#endif

        int verify = openjiuwen_verify_connection(&adapter->base);
        if (verify == 0) {
            adapter->conn_state = OPENJIUWEN_CONN_CONNECTED;
            adapter->consecutive_errors = 0;
            adapter->last_heartbeat_sec = get_timestamp();
            AIRY_LOG_INFO("OpenJiuwen: reconnected successfully on attempt %u", attempt + 1);
            return 0;
        }

        attempt++;
    }

    adapter->conn_state = OPENJIUWEN_CONN_ERROR;
    AIRY_LOG_ERROR("OpenJiuwen: reconnection failed after %u attempts", max_attempts);
    return AIRY_ERR_IO;
}

static int openjiuwen_send_with_retry(openjiuwen_adapter_t *adapter, const char *buffer,
                                      int buffer_len)
{
    if (!adapter || !buffer)
        return AIRY_ERR_NULL_POINTER;

    uint32_t attempt = 0;
    uint32_t max_attempts = adapter->config.max_retries > 0 ? adapter->config.max_retries + 1 : 1;

    while (attempt < max_attempts) {
        if (adapter->conn_state == OPENJIUWEN_CONN_ERROR ||
            adapter->conn_state == OPENJIUWEN_CONN_DISCONNECTED) {
            int rc = openjiuwen_reconnect(adapter);
            if (rc != 0)
                return rc;
        }

        adapter->message_counter++;
        adapter->last_activity_ms = get_timestamp_ms();

        if (adapter->consecutive_errors >= OPENJIUWEN_MAX_CONSECUTIVE_ERRORS) {
            AIRY_LOG_ERROR("OpenJiuwen: too many consecutive errors (%u), forcing reconnect",
                      adapter->consecutive_errors);
            adapter->conn_state = OPENJIUWEN_CONN_ERROR;
            if (openjiuwen_reconnect(adapter) != 0)
                return AIRY_ERR_IO;
            continue;
        }

        adapter->consecutive_errors = 0;
        return 0;

        attempt++;
    }

    return AIRY_ERR_IO;
}

/* ============================================================================

 * ============================================================================ */

static int openjiuwen_adapter_init(void *context)
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

static int openjiuwen_adapter_encode(void *context, const void *msg, void **out_data,
                                     size_t *out_size)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !msg || !out_data || !out_size)
        return AIRY_ERR_NULL_POINTER;
    if (!adapter->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    const unified_message_t *message = (const unified_message_t *)msg;
    char buffer[OPENJIUWEN_MAX_MESSAGE_SIZE];
    int result = openjiuwen_unified_to_native(message, buffer, sizeof(buffer));
    if (result < 0)
        return AIRY_ERR_NULL_POINTER;

    void *encoded = AIRY_MALLOC((size_t)result);
    if (!encoded)
        return AIRY_ERR_OUT_OF_MEMORY;
    __builtin_memcpy(encoded, buffer, (size_t)result);
    *out_data = encoded;
    *out_size = (size_t)result;
    return 0;
}

static int openjiuwen_adapter_decode(void *context, const void *data, size_t size, void *out_msg)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !data || !out_msg)
        return AIRY_ERR_NULL_POINTER;
    if (!adapter->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    unified_message_t *msg = (unified_message_t *)out_msg;
    int result = openjiuwen_native_to_unified(data, size, msg);
    if (result < 0)
        return AIRY_ERR_NULL_POINTER;
    return 0;
}

static int openjiuwen_adapter_connect(void *context, const char *endpoint)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;
    if (!endpoint)
        return AIRY_ERR_INVALID_PARAM;

    safe_strcpy(adapter->config.endpoint, endpoint, sizeof(adapter->config.endpoint));
    adapter->conn_state = OPENJIUWEN_CONN_CONNECTING;

    int verify = openjiuwen_verify_connection(&adapter->base);
    if (verify == 0) {
        adapter->conn_state = OPENJIUWEN_CONN_CONNECTED;
        adapter->last_heartbeat_sec = get_timestamp();
        AIRY_LOG_INFO("OpenJiuwen: connected to %s", endpoint);
        return 0;
    }

    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    AIRY_LOG_WARN("OpenJiuwen: connection to %s failed (verify=%d)", endpoint, verify);
    return AIRY_ERR_NULL_POINTER;
}

static int openjiuwen_adapter_disconnect(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_EINVAL;

    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    adapter->last_heartbeat_sec = 0;
    AIRY_LOG_INFO("OpenJiuwen: disconnected");
    return 0;
}

__attribute__((unused)) static int openjiuwen_adapter_deinit(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;

    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    adapter->connection_handle = NULL;
    adapter->consecutive_errors = 0;
    AIRY_LOG_INFO("OpenJiuwen: disconnected");
    return 0;
}

static int openjiuwen_adapter_is_connected(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return 0;
    return adapter->conn_state == OPENJIUWEN_CONN_CONNECTED ? 1 : 0;
}

static int openjiuwen_adapter_handle_request(void *context, const void *req, void **resp)
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
    response->message_id = generate_message_id();
    response->timestamp = get_timestamp();
    safe_strcpy(response->source_agent, "OpenJiuwen", sizeof(response->source_agent));
    safe_strcpy(response->target_agent, request->source_agent, sizeof(response->target_agent));
    response->payload = NULL;
    response->payload_size = 0;

    adapter->message_counter++;
    adapter->last_activity_ms = get_timestamp_ms();
    *resp = response;
    return 0;
}

static int openjiuwen_adapter_get_version(void *context, char *version_buf, size_t max_size)
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

static uint32_t openjiuwen_adapter_capabilities(void *context)
{
    return 0x0F;
}

static int openjiuwen_adapter_get_stats(void *context, char *stats_json, size_t max_size)
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

static int openjiuwen_send_message(void *context, const void *data, size_t size)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter || !data) {
        return AIRY_ERR_NULL_POINTER;
    }

    if (!adapter->initialized) {
        AIRY_LOG_ERROR("OpenJiuwen adapter not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    const unified_message_t *message = (const unified_message_t *)data;

    char buffer[OPENJIUWEN_MAX_MESSAGE_SIZE];
    int result = openjiuwen_unified_to_native(message, buffer, sizeof(buffer));
    if (result < 0) {
        AIRY_LOG_ERROR("Failed to convert message to OpenJiuwen format");
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-result);
        return AIRY_ERR_NULL_POINTER;
    }

    int send_result = openjiuwen_send_with_retry(adapter, buffer, result);
    if (send_result != 0) {
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-send_result);
        AIRY_LOG_ERROR("OpenJiuwen: send failed after retries (errors=%u)", adapter->consecutive_errors);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    uint32_t now = get_timestamp();
    if (now - adapter->last_heartbeat_sec >= OPENJIUWEN_HEARTBEAT_INTERVAL_SEC) {
        adapter->last_heartbeat_sec = now;
    }

    AIRY_LOG_DEBUG("Message sent to OpenJiuwen (id=%u, size=%d bytes)", adapter->message_counter,
              result);

    return (int)size;
}

/**
  * @brief Receive a message from the OpenJiuwen platform
 */
static int openjiuwen_receive_message(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter || !data) {
        return AIRY_ERR_NULL_POINTER;
    }

    if (!adapter->initialized) {
        AIRY_LOG_ERROR("OpenJiuwen adapter not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    if (adapter->conn_state != OPENJIUWEN_CONN_CONNECTED) {
        AIRY_LOG_WARN("OpenJiuwen: cannot receive - not connected (state=%d)", adapter->conn_state);
        return AIRY_ERR_NULL_POINTER;
    }

    unified_message_t *msg = (unified_message_t *)AIRY_CALLOC(1, sizeof(unified_message_t));
    if (!msg)
        return AIRY_ERR_OUT_OF_MEMORY;

    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    msg->message_id = generate_message_id();
    msg->timestamp = get_timestamp();

    adapter->last_activity_ms = get_timestamp_ms();

    uint32_t now = get_timestamp();
    if (now - adapter->last_heartbeat_sec >= OPENJIUWEN_HEARTBEAT_INTERVAL_SEC) {
        adapter->last_heartbeat_sec = now;
    }

    *data = msg;
    if (size)
        *size = sizeof(unified_message_t);
    return 0;
}

/**
  * @brief Destroy an adapter instance
 */
static int openjiuwen_destroy(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter) {
        return 0;
    }

    if (adapter->connection_handle) {
        adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
        adapter->connection_handle = NULL;
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

int openjiuwen_unified_to_native(const unified_message_t *msg, void *out_buffer, size_t buffer_size)
{
    if (!msg || !out_buffer || buffer_size < sizeof(openjiuwen_header_t)) {
        return AIRY_ERR_NULL_POINTER;
    }

    openjiuwen_header_t header;
    AIRY_MEMSET(&header, 0, sizeof(header));

    header.message_id = generate_message_id();
    header.timestamp = get_timestamp();
    header.message_type = OPENJIUWEN_MSG_TYPE_REQUEST;
    header.flags = 0x0001;

    safe_strcpy(header.source_agent, msg->source_agent, sizeof(header.source_agent));
    safe_strcpy(header.target_agent, "OpenJiuwen", sizeof(header.target_agent));

    size_t payload_length = 0;
    if (msg->payload && msg->payload_size > 0) {
        payload_length = msg->payload_size;
    }
    header.payload_length = (uint32_t)payload_length;

    size_t total_size = sizeof(openjiuwen_header_t) + payload_length;
    if (total_size > buffer_size) {
        AIRY_LOG_ERROR("Buffer too small for OpenJiuwen message");
        return AIRY_ERR_IO;
    }

    __builtin_memcpy(out_buffer, &header, sizeof(openjiuwen_header_t));

    if (payload_length > 0 && msg->payload) {
        __builtin_memcpy((char *)out_buffer + sizeof(openjiuwen_header_t), msg->payload,
                         payload_length);
    }

    return (int)total_size;
}

int openjiuwen_native_to_unified(const void *in_buffer, size_t buffer_size, unified_message_t *msg)
{
    if (!in_buffer || !msg || buffer_size < sizeof(openjiuwen_header_t)) {
        return AIRY_ERR_NULL_POINTER;
    }

    const openjiuwen_header_t *header = (const openjiuwen_header_t *)in_buffer;

    if (buffer_size < sizeof(openjiuwen_header_t) + header->payload_length) {
        AIRY_LOG_ERROR("Invalid OpenJiuwen message: incomplete data");
        return AIRY_ERR_INVALID_PARAM;
    }

    AIRY_MEMSET(msg, 0, sizeof(unified_message_t));

    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    msg->message_id = header->message_id;
    msg->timestamp = header->timestamp;

    safe_strcpy(msg->source_agent, header->source_agent, sizeof(msg->source_agent));
    safe_strcpy(msg->target_agent, header->target_agent, sizeof(msg->target_agent));

    if (header->payload_length > 0) {
        msg->payload_size = header->payload_length;
        msg->payload = AIRY_MALLOC(header->payload_length);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, (const char *)in_buffer + sizeof(openjiuwen_header_t),
                             header->payload_length);
        } else {
            msg->payload_size = 0;
            return AIRY_ERR_NULL_POINTER;
        }
    }

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
        return AIRY_ERR_NULL_POINTER;
    }

    uint32_t now = get_timestamp();
    uint32_t idle_seconds = now - impl->last_heartbeat_sec;
    if (idle_seconds > OPENJIUWEN_HEARTBEAT_INTERVAL_SEC * 3) {
        impl->conn_state = OPENJIUWEN_CONN_RECONNECTING;
        AIRY_LOG_WARN("OpenJiuwen: connection stale (idle=%us), needs reconnect", idle_seconds);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    impl->conn_state = OPENJIUWEN_CONN_CONNECTED;
    impl->last_heartbeat_sec = now;
    AIRY_LOG_INFO("OpenJiuwen connection verification successful");

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