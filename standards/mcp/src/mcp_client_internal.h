// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_client_internal.h
 * @brief Internal types and cross-file declarations shared by the MCP client split files.
 */

#ifndef MCP_CLIENT_INTERNAL_H
#define MCP_CLIENT_INTERNAL_H

#include "mcp_client.h"

#include <cjson/cJSON.h>
#include <cjson_helpers.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef _WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define MCP_CLIENT_READ_CHUNK 4096
#define MCP_CLIENT_HEADER_MAX 8192

typedef enum { MCP_FRAME_STATE_HEADER = 0, MCP_FRAME_STATE_BODY = 1 } mcp_frame_state_t;

/**
  * @brief Frame parsing state machine
 *
  * buf may hold partial or multiple frames; consumed bytes are memmove'd away
  * after a successful take; the rest feeds the next frame.
 */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    mcp_frame_state_t state;
    size_t hdr_end;
    size_t body_expected; /* Content-Length */
    char *header_text;
} mcp_frame_parser_t;

struct mcp_client_s {
    char *name;
    mcp_client_transport_t transport;

    pid_t child_pid;
    int stdio_write_fd;
    int stdio_read_fd;
    char **argv_copy;

    char *http_host;
    char *http_path;
    uint16_t http_port;

    mcp_frame_parser_t parser;

    uint64_t request_id;
    bool initialized;
    char *server_name;
};

/* Frame parsing domain (formerly static; referenced by stdio/http/rpc domains, now external) */
void frame_parser_init(mcp_frame_parser_t *p);
void frame_parser_destroy(mcp_frame_parser_t *p);
int frame_parser_append(mcp_frame_parser_t *p, const char *data, size_t n);
int frame_parser_take_frame(mcp_frame_parser_t *p, char **out);

/* Stdio transport domain (formerly static; referenced by rpc/lifecycle domains, now external) */
int write_all_with_timeout(int fd, const char *data, size_t len, int timeout_ms);
int stdio_send_message(mcp_client_t *c, const char *body);
int stdio_read_message(mcp_client_t *c, char **out, int timeout_ms);
char **copy_argv(char *const argv[]);
void free_argv(char **argv);

/* HTTP transport domain (formerly static; referenced by rpc/lifecycle domains, now external) */
int parse_http_url(const char *url, char **host_out, uint16_t *port_out, char **path_out);
int http_send_message(mcp_client_t *c, const char *body, char **out_body);

/* Protocol handshake/RPC domain (formerly static; referenced by tool-call domain, now external) */
int client_rpc_exchange(mcp_client_t *c, const char *method, const char *params_json,
                        bool expect_response, char **out_response);
int client_ensure_initialized(mcp_client_t *c);

#endif /* MCP_CLIENT_INTERNAL_H */
