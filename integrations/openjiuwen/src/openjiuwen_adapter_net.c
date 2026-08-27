// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter_net.c
 * @brief OpenJiuwen 协议适配器-网络传输域：endpoint 解析、TCP 连接/
 *        断开/重连与带重试的发送原语。
 *        自 openjiuwen_adapter.c 按功能域拆分，无外部 API 变化。
 */

#include "openjiuwen_adapter.h"
#include "openjiuwen_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"
#include "network_common.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <time.h>

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

int openjiuwen_net_connect(openjiuwen_adapter_t *adapter)
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
    adapter->last_heartbeat_sec = openjiuwen_get_timestamp();
    AIRY_LOG_INFO("OpenJiuwen: connected to %s:%u", cfg.host, port);
    return 0;
}

void openjiuwen_net_disconnect(openjiuwen_adapter_t *adapter)
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
int openjiuwen_send_with_retry(openjiuwen_adapter_t *adapter, const char *buffer,
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
        adapter->last_activity_ms = openjiuwen_get_timestamp_ms();
        return buffer_len;
    }

    return AIRY_ERR_IO;
}
