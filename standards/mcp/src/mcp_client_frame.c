// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client_frame.c
 * @brief MCP client frame-parsing state machine domain.
 *
 * Single responsibility: an accumulating buffer-parsing state machine for
 * LSP-style Content-Length frames (shared by stdio and HTTP responses),
 * handling multiple/partial frames per read; oversized Content-Length and
 * malformed frames return MCP_CLIENT_ERR_FRAME.
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
  * @brief Find "\r\n\r\n" within buf[0..len)
  * @return true if found; *out is the offset after the separator
 */
static bool find_header_end(const char *buf, size_t len, size_t *out)
{
    if (len < 4)
        return false;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *out = i + 4;
            return true;
        }
    }
    return false;
}

/**
  * @brief Parse Content-Length from frame headers (case-insensitive, multi-header)
  * @return 0 on success; -1 not found or malformed
 */
static int parse_content_length(const char *hdr, size_t hdr_len, size_t *out)
{
    size_t pos = 0;
    while (pos < hdr_len) {

        size_t line_end = pos;
        while (line_end + 1 < hdr_len && !(hdr[line_end] == '\r' && hdr[line_end + 1] == '\n'))
            line_end++;
        if (line_end + 1 >= hdr_len)
            break;
        size_t line_len = line_end - pos;
        if (line_len >= 15 && strncasecmp(hdr + pos, "content-length:", 15) == 0) {
            size_t v = pos + 15;
            while (v < line_end && (hdr[v] == ' ' || hdr[v] == '\t'))
                v++;
            char num_buf[32];
            size_t n = 0;
            while (v < line_end && n < sizeof(num_buf) - 1 && isdigit((unsigned char)hdr[v]))
                num_buf[n++] = hdr[v++];
            num_buf[n] = '\0';
            if (n == 0)
                return -1;
            *out = (size_t)strtoul(num_buf, NULL, 10);
            return 0;
        }
        pos = line_end + 2;
    }
    return -1;
}

void frame_parser_init(mcp_frame_parser_t *p)
{
    AIRY_MEMSET(p, 0, sizeof(*p));
    p->state = MCP_FRAME_STATE_HEADER;
}

void frame_parser_destroy(mcp_frame_parser_t *p)
{
    if (!p)
        return;
    AIRY_FREE(p->buf);
    AIRY_FREE(p->header_text);
    AIRY_MEMSET(p, 0, sizeof(*p));
}

int frame_parser_append(mcp_frame_parser_t *p, const char *data, size_t n)
{
    if (n == 0)
        return 0;
    if (p->len + n > MCP_CLIENT_MAX_MESSAGE_SIZE + MCP_CLIENT_HEADER_MAX) {
        AIRY_LOG_WARN("mcp client: frame buffer overflow (len=%zu, n=%zu)", p->len, n);
        return AIRY_ERR_OVERFLOW;
    }
    if (p->len + n > p->cap) {
        size_t new_cap = p->cap ? p->cap : MCP_CLIENT_READ_CHUNK;
        while (new_cap < p->len + n)
            new_cap *= 2;
        char *nb = AIRY_REALLOC(p->buf, new_cap);
        if (!nb)
            return AIRY_ERR_OUT_OF_MEMORY;
        p->buf = nb;
        p->cap = new_cap;
    }
    AIRY_MEMCPY(p->buf + p->len, data, n);
    p->len += n;
    return 0;
}

/**
  * @brief Try to extract one frame from the accumulated buffer
  * @return 0 complete frame (*out is the body, AIRY_MALLOC);
  *         1 not enough data yet;
  *         <0 error code
 */
int frame_parser_take_frame(mcp_frame_parser_t *p, char **out)
{
    if (p->state == MCP_FRAME_STATE_HEADER) {
        size_t hdr_end = 0;
        if (!find_header_end(p->buf, p->len, &hdr_end))
            return 1;
        size_t clen = 0;
        if (parse_content_length(p->buf, hdr_end, &clen) != 0) {
            AIRY_LOG_WARN("mcp client: frame header missing Content-Length");
            return MCP_CLIENT_ERR_FRAME;
        }
        if (clen == 0 || clen > MCP_CLIENT_MAX_MESSAGE_SIZE) {
            AIRY_LOG_WARN("mcp client: invalid Content-Length %zu", clen);
            return MCP_CLIENT_ERR_FRAME;
        }

        AIRY_FREE(p->header_text);
        p->header_text = AIRY_STRNDUP(p->buf, hdr_end);
        if (!p->header_text)
            return AIRY_ERR_OUT_OF_MEMORY;
        p->state = MCP_FRAME_STATE_BODY;
        p->hdr_end = hdr_end;
        p->body_expected = clen;
    }
    size_t frame_total = p->hdr_end + p->body_expected;
    if (p->len < frame_total)
        return 1;
    *out = AIRY_STRNDUP(p->buf + p->hdr_end, p->body_expected);
    if (!*out)
        return AIRY_ERR_OUT_OF_MEMORY;
    size_t remain = p->len - frame_total;
    if (remain > 0)
        AIRY_MEMCPY(p->buf, p->buf + frame_total, remain);
    p->len = remain;
    p->state = MCP_FRAME_STATE_HEADER;
    p->hdr_end = 0;
    p->body_expected = 0;
    return 0;
}
