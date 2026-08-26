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
#include "network_common.h"

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

/* ============ Real TCP transport over commons network_common ============ */

/* Parse "http://host:port/path" or "host:port" into scheme/host/port.
 * Returns 0 on success, negative on failure. Caller frees scheme/host. */
static int openjiuwen_parse_endpoint(const char *endpoint, char **out_scheme, char **out_host,
                                     uint16_t *out_port, char **out_path)
{
    if (!endpoint || !endpoint[0])
        return AIRY_ERR_INVALID_PARAM;

    const char *scheme = "tcp";
    const char *rest = endpoint;
    const char *colon_slash = strstr(endpoint, "://");
    if (colon_slash) {
        scheme = endpoint;
        rest = colon_slash + 3;
    }

    const char *host_start = rest;
    const char *host_end = host_start;
    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    const char *port_start = NULL;
    const char *port_end = NULL;
    if (*host_end == ':') {
        port_start = host_end + 1;
        port_end = port_start;
        while (*port_end && *port_end != '/')
            port_end++;
    }

    const char *path_start = *host_end == '/' ? host_end : NULL;

    uint16_t port = 0;
    if (port_start && port_end > port_start) {
        long p = strtol(port_start, NULL, 10);
        if (p <= 0 || p > 65535)
            return AIRY_ERR_INVALID_PARAM;
        port = (uint16_t)p;
    } else {
        port = strcmp(scheme, "https") == 0 ? 443 : 80;
    }

    char *host_buf = AIRY_MALLOC((size_t)(host_end - host_start) + 1);
    if (!host_buf)
        return AIRY_ERR_OUT_OF_MEMORY;
    __builtin_memcpy(host_buf, host_start, (size_t)(host_end - host_start));
    host_buf[host_end - host_start] = '\0';
    if (host_buf[0] == '\0') {
        AIRY_FREE(host_buf);
        return AIRY_ERR_INVALID_PARAM;
    }

    char *scheme_buf = AIRY_STRDUP(scheme);
    if (!scheme_buf) {
        AIRY_FREE(host_buf);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    char *path_buf = NULL;
    if (path_start && path_start[0]) {
        path_buf = AIRY_STRDUP(path_start);
        if (!path_buf) {
            AIRY_FREE(host_buf);
            AIRY_FREE(scheme_buf);
            return AIRY_ERR_OUT_OF_MEMORY;
        }
    } else {
        path_buf = AIRY_STRDUP("/");
        if (!path_buf) {
            AIRY_FREE(host_buf);
            AIRY_FREE(scheme_buf);
            return AIRY_ERR_OUT_OF_MEMORY;
        }
    }

    *out_scheme = scheme_buf;
    *out_host = host_buf;
    *out_port = port;
    *out_path = path_buf;
    return 0;
}

static int openjiuwen_net_connect(openjiuwen_adapter_t *adapter)
{
    char *scheme = NULL, *host = NULL, *path = NULL;
    uint16_t port = 0;
    int rc = openjiuwen_parse_endpoint(adapter->config.endpoint, &scheme, &host, &port, &path);
    if (rc != 0) {
        AIRY_LOG_ERROR("OpenJiuwen: invalid endpoint '%s'", adapter->config.endpoint);
        return rc;
    }

    network_config_t cfg = network_create_default_config();
    cfg.host = host;
    cfg.port = (int)port;
    cfg.timeout_ms = adapter->config.timeout_ms > 0 ? adapter->config.timeout_ms :
                                                       OPENJIUWEN_TIMEOUT_MS;
    cfg.read_timeout_ms = cfg.timeout_ms;
    cfg.write_timeout_ms = cfg.timeout_ms;
    cfg.sock_type = NETWORK_SOCK_STREAM;
    cfg.af = NETWORK_AF_UNSPEC;
    cfg.keepalive = true;
    cfg.nonblocking = false;
    cfg.ssl_enable = strcmp(scheme, "https") == 0;
    if (cfg.ssl_enable)
        cfg.ssl_verify = NETWORK_SSL_VERIFY_PEER;

    network_connection_t *conn = network_connection_create(&cfg);
    AIRY_FREE(scheme);
    AIRY_FREE(host);
    AIRY_FREE(path);
    if (!conn) {
        AIRY_LOG_ERROR("OpenJiuwen: connection allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    airy_err_t err = network_connect(conn);
    if (err != AIRY_SUCCESS) {
        AIRY_LOG_WARN("OpenJiuwen: connect to %s:%u failed (err=%d)", cfg.host, port, (int)err);
        network_connection_destroy(conn);
        return err;
    }

    adapter->connection_handle = conn;
    adapter->conn_state = OPENJIUWEN_CONN_CONNECTED;
    adapter->last_heartbeat_sec = get_timestamp();
    AIRY_LOG_INFO("OpenJiuwen: connected to %s:%u", cfg.host, port);
    return 0;
}

static void openjiuwen_net_disconnect(openjiuwen_adapter_t *adapter)
{
    if (adapter->connection_handle) {
        network_connection_t *conn = (network_connection_t *)adapter->connection_handle;
        network_disconnect(conn);
        network_connection_destroy(conn);
        adapter->connection_handle = NULL;
    }
    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
}

static int openjiuwen_reconnect(openjiuwen_adapter_t *adapter)
{
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;

    adapter->conn_state = OPENJIUWEN_CONN_RECONNECTING;
    adapter->total_reconnects++;

    uint32_t max_attempts = adapter->config.max_retries > 0 ? adapter->config.max_retries : 3;

    for (uint32_t attempt = 0; attempt < max_attempts; attempt++) {
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

        openjiuwen_net_disconnect(adapter);
        int rc = openjiuwen_net_connect(adapter);
        if (rc == 0) {
            adapter->consecutive_errors = 0;
            AIRY_LOG_INFO("OpenJiuwen: reconnected successfully on attempt %u", attempt + 1);
            return 0;
        }
    }

    adapter->conn_state = OPENJIUWEN_CONN_ERROR;
    AIRY_LOG_ERROR("OpenJiuwen: reconnection failed after %u attempts", max_attempts);
    return AIRY_ERR_IO;
}

/* Real send with retry over the TCP connection. Returns bytes sent or a
 * negative airy error code. Never reports success without a real write. */
static int openjiuwen_send_with_retry(openjiuwen_adapter_t *adapter, const char *buffer,
                                      int buffer_len)
{
    if (!adapter || !buffer || buffer_len <= 0)
        return AIRY_ERR_NULL_POINTER;

    uint32_t max_attempts = adapter->config.max_retries > 0 ? adapter->config.max_retries + 1 : 1;

    for (uint32_t attempt = 0; attempt < max_attempts; attempt++) {
        if (adapter->conn_state != OPENJIUWEN_CONN_CONNECTED || !adapter->connection_handle) {
            int rc = openjiuwen_reconnect(adapter);
            if (rc != 0)
                return rc;
        }

        network_connection_t *conn = (network_connection_t *)adapter->connection_handle;
        airy_err_t err = network_send_all(conn, buffer, (size_t)buffer_len);
        if (err != AIRY_SUCCESS) {
            adapter->consecutive_errors++;
            adapter->last_error_code = (uint32_t)(-err);
            AIRY_LOG_WARN("OpenJiuwen: send attempt %u/%u failed (err=%d)", attempt + 1, max_attempts,
                     (int)err);
            openjiuwen_net_disconnect(adapter);
            if (attempt + 1 >= max_attempts)
                return err;
            int rc = openjiuwen_reconnect(adapter);
            if (rc != 0)
                return rc;
            continue;
        }

        adapter->consecutive_errors = 0;
        adapter->message_counter++;
        adapter->last_activity_ms = get_timestamp_ms();
        return buffer_len;
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
    if (endpoint && endpoint[0])
        safe_strcpy(adapter->config.endpoint, endpoint, sizeof(adapter->config.endpoint));

    adapter->conn_state = OPENJIUWEN_CONN_CONNECTING;

    int rc = openjiuwen_net_connect(adapter);
    if (rc == 0)
        return 0;

    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    AIRY_LOG_WARN("OpenJiuwen: connection to %s failed (rc=%d)", adapter->config.endpoint, rc);
    return rc;
}

static int openjiuwen_adapter_disconnect(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_EINVAL;

    openjiuwen_net_disconnect(adapter);
    adapter->last_heartbeat_sec = 0;
    AIRY_LOG_INFO("OpenJiuwen: disconnected");
    return 0;
}

__attribute__((unused)) static int openjiuwen_adapter_deinit(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;

    openjiuwen_net_disconnect(adapter);
    adapter->consecutive_errors = 0;
    AIRY_LOG_INFO("OpenJiuwen: deinitialized");
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

    if (adapter->conn_state != OPENJIUWEN_CONN_CONNECTED || !adapter->connection_handle) {
        AIRY_LOG_WARN("OpenJiuwen: cannot receive - not connected (state=%d)", adapter->conn_state);
        return AIRY_ENOTCONN;
    }

    network_connection_t *conn = (network_connection_t *)adapter->connection_handle;
    network_set_timeout(conn, (int)(timeout_ms > 0 ? timeout_ms : OPENJIUWEN_TIMEOUT_MS));

    /* Read the fixed frame header first. */
    openjiuwen_header_t header;
    AIRY_MEMSET(&header, 0, sizeof(header));
    size_t received = 0;
    airy_err_t err = network_receive(conn, &header, sizeof(header), &received);
    if (err != AIRY_SUCCESS || received < sizeof(header)) {
        if (err == AIRY_ERR_TIMEOUT || err == AIRY_ERR_WOULD_BLOCK)
            return err;
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-err);
        openjiuwen_net_disconnect(adapter);
        return err != AIRY_SUCCESS ? err : AIRY_ERR_IO;
    }

    /* Validate header fields before trusting payload_length. */
    if (header.message_id == 0 || header.payload_length > OPENJIUWEN_MAX_MESSAGE_SIZE) {
        AIRY_LOG_WARN("OpenJiuwen: invalid frame header (id=%u len=%u)", header.message_id,
                 header.payload_length);
        return AIRY_ERR_PARSE_ERROR;
    }

    uint8_t *frame = (uint8_t *)AIRY_MALLOC(sizeof(header) + header.payload_length);
    if (!frame)
        return AIRY_ERR_OUT_OF_MEMORY;
    __builtin_memcpy(frame, &header, sizeof(header));

    size_t payload_read = 0;
    if (header.payload_length > 0) {
        err = network_receive(conn, frame + sizeof(header), header.payload_length, &payload_read);
        if (err != AIRY_SUCCESS || payload_read != header.payload_length) {
            AIRY_FREE(frame);
            adapter->consecutive_errors++;
            openjiuwen_net_disconnect(adapter);
            return err != AIRY_SUCCESS ? err : AIRY_ERR_IO;
        }
    }

    unified_message_t *msg = (unified_message_t *)AIRY_CALLOC(1, sizeof(unified_message_t));
    if (!msg) {
        AIRY_FREE(frame);
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    msg->message_id = header.message_id;
    msg->timestamp = header.timestamp;
    safe_strcpy(msg->source_agent, header.source_agent, sizeof(msg->source_agent));
    safe_strcpy(msg->target_agent, header.target_agent, sizeof(msg->target_agent));
    if (header.payload_length > 0) {
        msg->payload = AIRY_MALLOC(header.payload_length);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, frame + sizeof(header), header.payload_length);
            msg->payload_size = header.payload_length;
        } else {
            AIRY_FREE(frame);
            AIRY_FREE(msg);
            return AIRY_ERR_OUT_OF_MEMORY;
        }
    }
    AIRY_FREE(frame);

    adapter->consecutive_errors = 0;
    adapter->last_activity_ms = get_timestamp_ms();

    uint32_t now = get_timestamp();
    if (now - adapter->last_heartbeat_sec >= OPENJIUWEN_HEARTBEAT_INTERVAL_SEC)
        adapter->last_heartbeat_sec = now;

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
        return AIRY_ERR_IO;
    }

    /* A live connection that has been idle too long must be re-established. */
    uint32_t now = get_timestamp();
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