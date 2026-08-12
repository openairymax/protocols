// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client.c
 * @brief MCP v1.0 client implementation (stdio / http dual transport).
 *
 * This file keeps the client connection lifecycle (connect/disconnect), tool
 * list release and transport-type stringification. Frame parsing, stdio/http
 * transport, protocol handshake RPC and tool calls live in mcp_client_frame.c /
 * mcp_client_stdio.c / mcp_client_http.c / mcp_client_rpc.c / mcp_client_tool.c.
 *
 * All memory operations use the AIRY_MALLOC/AIRY_FREE/AIRY_MEMCPY macros
 * (BAN poison compliant); logging uses LOG_* (matching mcp_v1_adapter.c).
 */

#define LOG_TAG "mcp_client"

#include "mcp_client.h"
#include "mcp_client_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"

#include <cjson/cJSON.h>
#include <cjson_helpers.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifndef _WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

mcp_client_t *mcp_client_connect_stdio(const char *name, const char *command, char *const argv[])
{
#ifdef _WIN32
    (void)name;
    (void)command;
    (void)argv;
    LOG_WARN("mcp client: stdio transport not supported on Windows");
    return NULL;
#else
    if (!name || !name[0] || !command || !argv || !argv[0])
        return NULL;

    char **argv_copy = copy_argv(argv);
    if (!argv_copy)
        return NULL;

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        LOG_WARN("mcp client '%s': pipe() failed (errno=%d)", name, errno);
        if (to_child[0] >= 0) {
            close(to_child[0]);
            close(to_child[1]);
        }
        if (from_child[0] >= 0) {
            close(from_child[0]);
            close(from_child[1]);
        }
        free_argv(argv_copy);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("mcp client '%s': fork() failed (errno=%d)", name, errno);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        free_argv(argv_copy);
        return NULL;
    }
    if (pid == 0) {

        (void)dup2(to_child[0], STDIN_FILENO);
        (void)dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execvp(argv_copy[0], argv_copy);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);

    mcp_client_t *c = AIRY_CALLOC(1, sizeof(mcp_client_t));
    if (!c) {
        close(to_child[1]);
        close(from_child[0]);
        (void)kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        free_argv(argv_copy);
        return NULL;
    }
    c->name = AIRY_STRDUP(name);
    c->transport = MCP_CLIENT_TRANSPORT_STDIO;
    c->child_pid = pid;
    c->stdio_write_fd = to_child[1];
    c->stdio_read_fd = from_child[0];
    c->argv_copy = argv_copy;
    c->request_id = 0;
    c->initialized = false;
    frame_parser_init(&c->parser);
    LOG_INFO("mcp client '%s': stdio connected (pid=%d, command=%s)", name, (int)pid, command);
    return c;
#endif
}

mcp_client_t *mcp_client_connect_http(const char *name, const char *url)
{
    if (!name || !name[0] || !url)
        return NULL;
    char *host = NULL;
    char *path = NULL;
    uint16_t port = 0;
    if (parse_http_url(url, &host, &port, &path) != 0) {
        LOG_WARN("mcp client '%s': invalid http url: %s", name, url);
        return NULL;
    }
    mcp_client_t *c = AIRY_CALLOC(1, sizeof(mcp_client_t));
    if (!c) {
        AIRY_FREE(host);
        AIRY_FREE(path);
        return NULL;
    }
    c->name = AIRY_STRDUP(name);
    c->transport = MCP_CLIENT_TRANSPORT_HTTP;
    c->http_host = host;
    c->http_path = path;
    c->http_port = port;
    c->request_id = 0;
    c->initialized = false;
    frame_parser_init(&c->parser);
    LOG_INFO("mcp client '%s': http endpoint http://%s:%u%s", name, host, (unsigned)port, path);
    return c;
}

int mcp_client_disconnect(mcp_client_t *c)
{
    if (!c)
        return AIRY_ERR_INVALID_PARAM;
    if (c->transport == MCP_CLIENT_TRANSPORT_STDIO) {
        if (c->stdio_write_fd >= 0) {
            close(c->stdio_write_fd);
            c->stdio_write_fd = -1;
        }
        if (c->stdio_read_fd >= 0) {
            close(c->stdio_read_fd);
            c->stdio_read_fd = -1;
        }
        if (c->child_pid > 0) {

            (void)kill(c->child_pid, SIGTERM);
            for (int i = 0; i < 20; i++) {
                int st = 0;
                pid_t r = waitpid(c->child_pid, &st, WNOHANG);
                if (r == c->child_pid)
                    break;
                if (r < 0)
                    break;
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 100 * 1000 * 1000;
                (void)nanosleep(&ts, NULL);
            }
            int st = 0;
            if (waitpid(c->child_pid, &st, WNOHANG) == 0) {
                (void)kill(c->child_pid, SIGKILL);
                (void)waitpid(c->child_pid, &st, 0);
            }
            c->child_pid = 0;
        }
    }
    frame_parser_destroy(&c->parser);
    AIRY_FREE(c->name);
    free_argv(c->argv_copy);
    AIRY_FREE(c->http_host);
    AIRY_FREE(c->http_path);
    AIRY_FREE(c->server_name);
    AIRY_FREE(c);
    return 0;
}

void mcp_client_tool_list_free(mcp_client_tool_list_t *list)
{
    if (!list)
        return;
    if (list->tools) {
        for (size_t i = 0; i < list->count; i++) {
            AIRY_FREE(list->tools[i].name);
            AIRY_FREE(list->tools[i].description);
            AIRY_FREE(list->tools[i].input_schema_json);
        }
        AIRY_FREE(list->tools);
    }
    list->tools = NULL;
    list->count = 0;
}

const char *mcp_client_transport_string(mcp_client_transport_t t)
{
    switch (t) {
    case MCP_CLIENT_TRANSPORT_STDIO:
        return "stdio";
    case MCP_CLIENT_TRANSPORT_HTTP:
        return "http";
    default:
        return "none";
    }
}
