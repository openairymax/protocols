// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openclaw_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the OpenClaw adapter split files.
 */

#ifndef OPENCLAW_ADAPTER_INTERNAL_H
#define OPENCLAW_ADAPTER_INTERNAL_H

#include "openclaw_adapter.h"

#ifndef _WIN32
#include <pthread.h>
#endif

#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close_socket(s) closesocket(s)
#define sock_errno WSAGetLastError()
#define SOCK_EINPROGRESS WSAEINPROGRESS
typedef SOCKET socket_fd_t;
#define INVALID_SOCK (-1)
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define close_socket(s) close(s)
#define sock_errno errno
#define SOCK_EINPROGRESS EINPROGRESS
typedef int socket_fd_t;
#define INVALID_SOCK (-1)
#endif

#define OPENCLAW_SOCKET_TIMEOUT_MS 5000
#define OPENCLAW_RECV_BUFFER_SIZE 65536

struct openclaw_adapter_context_s {
    openclaw_config_t config;
    bool initialized;
    bool connected;
    openclaw_agent_card_t *registered_agents;
    size_t registered_agent_count;
    openclaw_session_t *active_sessions;
    size_t active_session_count;
    openclaw_tool_info_t *registered_tools;
    size_t registered_tool_count;
    openclaw_task_t *tracked_tasks;
    size_t tracked_task_count;
    openclaw_message_handler_t message_handler;
    void *message_handler_data;
    openclaw_task_handler_t task_handler;
    void *task_handler_data;
    openclaw_event_callback_t event_callback;
    void *event_callback_data;
    openclaw_status_callback_t status_callback;
    void *status_callback_data;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t tasks_delegated;
    uint64_t tasks_completed;
    uint64_t connection_uptime_sec;
    uint64_t connect_timestamp;
    socket_fd_t sock_fd;
    char *connected_endpoint;
    void *send_buffer;
    size_t send_buffer_size;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    char last_error[256];
};

/* Helpers shared across files (was static; now external linkage) **/
int openclaw_parse_endpoint(const char *endpoint_url, char *host, size_t host_size, int *port);
int openclaw_socket_connect(socket_fd_t fd, const char *host, int port, uint32_t timeout_ms);
int openclaw_socket_send(socket_fd_t fd, const void *data, size_t len, uint32_t timeout_ms);
int openclaw_socket_recv(socket_fd_t fd, char *buffer, size_t buffer_size, size_t *out_len,
                         uint32_t timeout_ms);
int openclaw_serialize_message(const openclaw_message_t *msg, char *buffer, size_t buffer_size);

/* Protocol adapter callbacks (formerly static; referenced by openclaw_get_protocol_adapter(), now external) */
int openclaw_proto_init(void *context);
int openclaw_proto_destroy(void *context);
int openclaw_proto_handle_request(void *context, const void *req, void **resp);
int openclaw_proto_get_version(void *context, char *buf, size_t max_size);
uint32_t openclaw_proto_capabilities(void *context);
int openclaw_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size);
int openclaw_proto_decode(void *context, const void *data, size_t size, void *out_msg);
int openclaw_proto_connect(void *context, const char *endpoint);
int openclaw_proto_disconnect(void *context);
int openclaw_proto_is_connected(void *context);
int openclaw_proto_send(void *context, const void *data, size_t size);
int openclaw_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms);
int openclaw_proto_get_stats(void *context, char *stats_json, size_t max_size);

#endif /* OPENCLAW_ADAPTER_INTERNAL_H */
