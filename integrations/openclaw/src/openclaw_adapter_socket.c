// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_socket.c
 * @brief OpenClaw adapter socket transport domain (endpoint resolution/connection/send-recv/message serialization).
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

int openclaw_parse_endpoint(const char *endpoint_url, char *host, size_t host_size, int *port)
{
    if (!endpoint_url || !host || !port) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_parse_endpoint: parse error");
        return AIRY_ERR_UNKNOWN;
    }

    const char *url = endpoint_url;
    const char *host_start = url;

    if (strncmp(url, "http://", 7) == 0) {
        host_start = url + 7;
        *port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        host_start = url + 8;
        *port = 443;
    } else {
        host_start = url;
        *port = 28080;
    }

    const char *colon = strchr(host_start, ':');
    const char *slash = strchr(host_start, '/');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = (size_t)(colon - host_start);
        if (host_len >= host_size)
            host_len = host_size - 1;
        __builtin_memcpy(host, host_start, host_len);
        host[host_len] = '\0';
        *port = (int)strtol(colon + 1, NULL, 10);
    } else if (slash) {
        size_t host_len = (size_t)(slash - host_start);
        if (host_len >= host_size)
            host_len = host_size - 1;
        __builtin_memcpy(host, host_start, host_len);
        host[host_len] = '\0';
    } else {
        size_t host_len = strlen(host_start);
        if (host_len >= host_size)
            host_len = host_size - 1;
        __builtin_memcpy(host, host_start, host_len);
        host[host_len] = '\0';
    }

    return 0;
}

static int __attribute__((unused)) openclaw_socket_set_nonblocking(socket_fd_t fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "ioctlsocket: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int openclaw_socket_connect(socket_fd_t fd, const char *host, int port, uint32_t timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;

    AIRY_MEMSET(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "snprintf: failed");
        return AIRY_ERR_UNKNOWN;
    }

    int connected = -1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            connected = 0;
            break;
        }
        if (sock_errno == SOCK_EINPROGRESS) {
            connected = 1;
            break;
        }
    }

    freeaddrinfo(result);

    if (connected == -1)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    if (connected == 1) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;

        int poll_ret = poll(&pfd, 1, (int)timeout_ms);
        if (poll_ret <= 0)
            AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len) < 0)
            AIRY_ERROR(AIRY_ERR_OUT_OF_MEMORY, "out of memory");
        if (so_error != 0)
            AIRY_ERROR(AIRY_ERR_IO, "I/O error");
    }

    return 0;
}

int openclaw_socket_send(socket_fd_t fd, const void *data, size_t len, uint32_t timeout_ms)
{
    size_t total_sent = 0;

    while (total_sent < len) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;

        int poll_ret = poll(&pfd, 1, (int)timeout_ms);
        if (poll_ret <= 0) {
            airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "poll: timeout");
            return AIRY_ERR_TIMEOUT;
        }

        ssize_t sent = send(fd, (const char *)data + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (sock_errno == EINTR)
                continue;
            return AIRY_EINVAL;
        }
        total_sent += (size_t)sent;
    }

    return 0;
}

int openclaw_socket_recv(socket_fd_t fd, char *buffer, size_t buffer_size, size_t *out_len,
                         uint32_t timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    int poll_ret = poll(&pfd, 1, (int)timeout_ms);
    if (poll_ret <= 0) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "poll: timeout");
        return AIRY_ERR_TIMEOUT;
    }

    ssize_t recvd = recv(fd, buffer, buffer_size - 1, 0);
    if (recvd < 0) {
        if (sock_errno == EINTR) {
            *out_len = 0;
            return 0;
        }
        return AIRY_EINVAL;
    }
    if (recvd == 0)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    buffer[recvd] = '\0';
    *out_len = (size_t)recvd;
    return 0;
}

int openclaw_serialize_message(const openclaw_message_t *msg, char *buffer, size_t buffer_size)
{
    if (!msg || !buffer) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_serialize_message: failed");
        return AIRY_ERR_UNKNOWN;
    }

    const char *sender = msg->sender_id ? msg->sender_id : "";
    const char *receiver = msg->receiver_id ? msg->receiver_id : "";
    const char *session = msg->session_id ? msg->session_id : "";
    const char *mid = msg->message_id ? msg->message_id : "";
    const char *ctype = msg->content_type ? msg->content_type : "text/plain";
    const char *pload = msg->payload ? (const char *)msg->payload : "";

    return snprintf(buffer, buffer_size,
                    "{"
                    "\"message_id\":\"%s\","
                    "\"session_id\":\"%s\","
                    "\"sender_id\":\"%s\","
                    "\"receiver_id\":\"%s\","
                    "\"modality\":%u,"
                    "\"content_type\":\"%s\","
                    "\"payload\":\"%s\","
                    "\"payload_size\":%zu,"
                    "\"timestamp\":%llu,"
                    "\"priority\":%u"
                    "}",
                    mid, session, sender, receiver, (unsigned int)msg->modality, ctype, pload,
                    msg->payload_size, (unsigned long long)msg->timestamp,
                    (unsigned int)msg->priority);
}
