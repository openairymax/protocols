// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client_http.c
 * @brief MCP client HTTP transport domain (Streamable HTTP short connections).
 *
 * Single responsibility: http:// endpoint parsing, TCP connection, POST request
 * building and response reading (reuses the frame state machine, with SSE
 * data: extraction support).
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

/**
  * @brief Parse http:
 */
int parse_http_url(const char *url, char **host_out, uint16_t *port_out, char **path_out)
{
    *host_out = NULL;
    *path_out = NULL;
    *port_out = 80;
    if (strncmp(url, "http://", 7) != 0)
        return -1;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    size_t host_len = slash ? (size_t)(slash - p) : strlen(p);
    if (host_len == 0)
        return -1;

    size_t colon = 0;
    while (colon < host_len && p[colon] != ':')
        colon++;

    char *host = NULL;
    uint16_t port = 80;
    if (colon < host_len) {
        size_t plen = host_len - colon - 1;
        if (plen == 0 || plen >= 8)
            return -1;
        char port_buf[8];
        AIRY_MEMCPY(port_buf, p + colon + 1, plen);
        port_buf[plen] = '\0';
        unsigned long pv = strtoul(port_buf, NULL, 10);
        if (pv == 0 || pv > 65535)
            return -1;
        port = (uint16_t)pv;
        host = AIRY_STRNDUP(p, colon);
    } else {
        host = AIRY_STRNDUP(p, host_len);
    }
    if (!host)
        return -1;
    const char *path = slash ? slash : "/mcp";
    char *path_copy = AIRY_STRDUP(path);
    if (!path_copy) {
        AIRY_FREE(host);
        return -1;
    }
    *host_out = host;
    *port_out = port;
    *path_out = path_copy;
    return 0;
}

static int http_connect_fd(mcp_client_t *c)
{
    struct addrinfo hints;
    AIRY_MEMSET(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)c->http_port);
    int grc = getaddrinfo(c->http_host, port_str, &hints, &res);
    if (grc != 0) {
        LOG_WARN("mcp client '%s': getaddrinfo failed for %s (%s)", c->name, c->http_host,
                 gai_strerror(grc));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        LOG_WARN("mcp client '%s': connect to %s:%u failed", c->name, c->http_host,
                 (unsigned)c->http_port);
    return fd;
}

static int parse_http_status(const char *hdr)
{
    if (strncmp(hdr, "HTTP/", 5) != 0)
        return 0;
    const char *p = hdr + 5;
    while (*p && *p != ' ')
        p++;
    while (*p == ' ')
        p++;
    char num[8];
    size_t i = 0;
    while (*p && isdigit((unsigned char)*p) && i < sizeof(num) - 1)
        num[i++] = *p++;
    num[i] = '\0';
    return i ? (int)strtol(num, NULL, 10) : 0;
}

/**
  * @brief Extract the value of the first "data:" line from an SSE stream
 */
static char *extract_sse_data(const char *body)
{
    if (!body)
        return NULL;
    const char *p = body;
    while (*p) {
        if (strncmp(p, "data:", 5) == 0 && (p == body || p[-1] == '\n')) {
            const char *v = p + 5;
            while (*v == ' ')
                v++;
            const char *e = v;
            while (*e && *e != '\n')
                e++;
            size_t len = (size_t)(e - v);
            if (len > 0 && v[len - 1] == '\r')
                len--;
            return AIRY_STRNDUP(v, len);
        }
        p++;
    }
    return NULL;
}

/**
  * @brief Read an HTTP response (reuses the frame state machine: header + length + body)
 */
static int http_read_response(mcp_client_t *c, int fd, char **out_body)
{
    mcp_frame_parser_t p;
    frame_parser_init(&p);
    int rc;
    for (;;) {
        rc = frame_parser_take_frame(&p, out_body);
        if (rc == 0)
            break;
        if (rc != 1) {
            frame_parser_destroy(&p);
            return rc;
        }
        char tmp[MCP_CLIENT_READ_CHUNK];
        ssize_t n = -1;
        do {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, MCP_CLIENT_DEFAULT_TIMEOUT_MS);
            if (pr == 0) {
                frame_parser_destroy(&p);
                LOG_WARN("mcp client '%s': http read timeout", c->name);
                return AIRY_ERR_TIMEOUT;
            }
            if (pr < 0) {
                if (errno == EINTR)
                    continue;
                frame_parser_destroy(&p);
                return AIRY_ERR_IO;
            }
            n = read(fd, tmp, sizeof(tmp));
        } while (n < 0 && errno == EINTR);
        if (n == 0) {
            frame_parser_destroy(&p);
            return MCP_CLIENT_ERR_PROCESS_EXIT;
        }
        if (n < 0) {
            frame_parser_destroy(&p);
            return AIRY_ERR_IO;
        }
        rc = frame_parser_append(&p, tmp, (size_t)n);
        if (rc != 0) {
            frame_parser_destroy(&p);
            return rc;
        }
    }

    int status = 0;
    bool is_sse = false;
    if (p.header_text) {
        status = parse_http_status(p.header_text);
        if (strstr(p.header_text, "text/event-stream") != NULL)
            is_sse = true;
    }
    if (status >= 400)
        LOG_WARN("mcp client '%s': http status %d", c->name, status);

    if (is_sse && *out_body) {

        char *data = extract_sse_data(*out_body);
        AIRY_FREE(*out_body);
        if (data) {
            *out_body = data;
        } else {
            *out_body = AIRY_STRDUP("");
        }
    }
    frame_parser_destroy(&p);
    return 0;
}

int http_send_message(mcp_client_t *c, const char *body, char **out_body)
{
    int fd = http_connect_fd(c);
    if (fd < 0)
        return MCP_CLIENT_ERR_CONNECT;

    size_t blen = strlen(body);
    size_t host_len = strlen(c->http_host);
    size_t path_len = strlen(c->http_path);
    size_t req_cap = 256 + host_len + path_len + blen;
    char *req = AIRY_MALLOC(req_cap);
    if (!req) {
        close(fd);
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    int rlen = snprintf(req, req_cap,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json, text/event-stream\r\n"
                        "Mcp-Protocol-Version: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        c->http_path, c->http_host, MCP_CLIENT_PROTOCOL_VERSION, blen, body);
    int rc = write_all_with_timeout(fd, req, (size_t)rlen, MCP_CLIENT_DEFAULT_TIMEOUT_MS);
    AIRY_FREE(req);
    if (rc != 0) {
        close(fd);
        return rc;
    }
    rc = http_read_response(c, fd, out_body);
    close(fd);
    return rc;
}
