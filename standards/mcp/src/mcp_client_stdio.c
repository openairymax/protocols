// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client_stdio.c
 * @brief MCP client stdio transport domain (child-process pipe send/recv, argv copy).
 *
 * Single responsibility: frame write/read on the stdin/stdout pipes (poll
 * timeout + EOF/child-exit detection) and argv deep-copy/free helpers.
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

int write_all_with_timeout(int fd, const char *data, size_t len, int timeout_ms)
{
    size_t off = 0;
    while (off < len) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0)
            return AIRY_ERR_TIMEOUT;
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return AIRY_ERR_IO;
        }
        if (pfd.revents & (POLLERR | POLLNVAL))
            return AIRY_ERR_IO;
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return AIRY_ERR_IO;
        }
        off += (size_t)n;
    }
    return 0;
}

int stdio_send_message(mcp_client_t *c, const char *body)
{
    size_t len = strlen(body);
    char header[64];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    int rc = write_all_with_timeout(c->stdio_write_fd, header, (size_t)hlen,
                                    MCP_CLIENT_DEFAULT_TIMEOUT_MS);
    if (rc != 0)
        return rc;
    return write_all_with_timeout(c->stdio_write_fd, body, len, MCP_CLIENT_DEFAULT_TIMEOUT_MS);
}

/**
  * @brief Read one frame from stdio (poll timeout + EOF/child-exit detection)
 */
int stdio_read_message(mcp_client_t *c, char **out, int timeout_ms)
{
    *out = NULL;
    for (;;) {
        int rc = frame_parser_take_frame(&c->parser, out);
        if (rc == 0)
            return 0;
        if (rc != 1)
            return rc;
        if (timeout_ms <= 0)
            return AIRY_ERR_TIMEOUT;

        char tmp[MCP_CLIENT_READ_CHUNK];
        struct pollfd pfd;
        pfd.fd = c->stdio_read_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0) {
            AIRY_LOG_WARN("mcp client '%s': read timeout (%d ms)", c->name, timeout_ms);
            return AIRY_ERR_TIMEOUT;
        }
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return AIRY_ERR_IO;
        }
        if (pfd.revents & POLLNVAL)
            return AIRY_ERR_IO;

        ssize_t n;
        do {
            n = read(c->stdio_read_fd, tmp, sizeof(tmp));
        } while (n < 0 && errno == EINTR);
        if (n == 0) {

            AIRY_LOG_WARN("mcp client '%s': stdio EOF, child process exited", c->name);
            return MCP_CLIENT_ERR_PROCESS_EXIT;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return AIRY_ERR_IO;
        }
        rc = frame_parser_append(&c->parser, tmp, (size_t)n);
        if (rc != 0)
            return rc;
    }
}

char **copy_argv(char *const argv[])
{
    size_t n = 0;
    while (argv[n])
        n++;
    char **copy = AIRY_CALLOC(n + 1, sizeof(char *));
    if (!copy)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        copy[i] = AIRY_STRDUP(argv[i]);
        if (!copy[i]) {
            for (size_t j = 0; j < i; j++)
                AIRY_FREE(copy[j]);
            AIRY_FREE(copy);
            return NULL;
        }
    }
    return copy;
}

void free_argv(char **argv)
{
    if (!argv)
        return;
    for (size_t i = 0; argv[i]; i++)
        AIRY_FREE(argv[i]);
    AIRY_FREE(argv);
}
