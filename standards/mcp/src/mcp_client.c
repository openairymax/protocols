// SPDX-FileCopyrightText: 2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
// @owner: team-B
/**
 * @file mcp_client.c
 * @brief MCP v1.0 Client Implementation（stdio / http 双传输）
 *
 * 帧协议（stdio 与 HTTP 响应共用）：
 *   Content-Length: <N>\r\n
 *   \r\n
 *   <N 字节 JSON body>
 *
 * 帧解析为状态机（MCP_FRAME_STATE_HEADER -> MCP_FRAME_STATE_BODY），
 * 内部累积缓冲区处理一次 read 多帧/半帧；Content-Length 超限与
 * 帧结构错误返回 MCP_CLIENT_ERR_FRAME。
 *
 * 内存操作一律使用 AIRY_MALLOC/AIRY_FREE/AIRY_MEMCPY 等宏（BAN poison 合规）；
 * 日志使用 LOG_*（与同目录 mcp_v1_adapter.c 一致）。
 */

#include "mcp_client.h"

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

#define MCP_CLIENT_READ_CHUNK 4096  /* 单次 read 缓冲 */
#define MCP_CLIENT_HEADER_MAX 8192  /* 帧头最大长度（防恶意头部撑爆缓冲） */

typedef enum {
    MCP_FRAME_STATE_HEADER = 0, /* 等待 Content-Length 头 + 空行 */
    MCP_FRAME_STATE_BODY = 1    /* 等待 body 数据 */
} mcp_frame_state_t;

/**
 * @brief 帧解析状态机
 *
 * buf 中同时可能存有半帧或多帧数据；take 成功后已消费字节被 memmove 移除，
 * 剩余数据继续参与下一帧解析。
 */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    mcp_frame_state_t state;
    size_t hdr_end;        /* "\r\n\r\n" 结束偏移（BODY 状态有效） */
    size_t body_expected;  /* Content-Length */
    char *header_text;     /* 最近一帧的头部副本（HTTP 层解析状态码用） */
} mcp_frame_parser_t;

struct mcp_client_s {
    char *name;
    mcp_client_transport_t transport;

    /* stdio 传输 */
    pid_t child_pid;
    int stdio_write_fd; /* 父 -> 子 stdin */
    int stdio_read_fd;  /* 子 stdout -> 父 */
    char **argv_copy;   /* 深拷贝的命令行（argv[0] 即 command） */

    /* http 传输 */
    char *http_host;
    char *http_path;
    uint16_t http_port;

    /* 帧解析状态机 */
    mcp_frame_parser_t parser;

    /* JSON-RPC 状态 */
    uint64_t request_id;
    bool initialized;
    char *server_name; /* initialize 响应中 serverInfo.name */
};

/* ==================== 工具函数 ==================== */

static int64_t now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

/**
 * @brief JSON 字符串转义并加引号（与 mcp_v1_adapter.c 的 json_string_escape 一致）
 * @return 带引号的 JSON 字符串（AIRY_MALLOC）
 */
static char *json_string_escape(const char *str)
{
    if (!str)
        return AIRY_STRDUP("null");
    size_t len = strlen(str);
    size_t escaped_len = len * 2 + 3;
    char *escaped = AIRY_MALLOC(escaped_len);
    if (!escaped)
        return NULL;
    size_t j = 0;
    escaped[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
        case '"':
            escaped[j++] = '\\';
            escaped[j++] = '"';
            break;
        case '\\':
            escaped[j++] = '\\';
            escaped[j++] = '\\';
            break;
        case '\n':
            escaped[j++] = '\\';
            escaped[j++] = 'n';
            break;
        case '\r':
            escaped[j++] = '\\';
            escaped[j++] = 'r';
            break;
        case '\t':
            escaped[j++] = '\\';
            escaped[j++] = 't';
            break;
        default:
            escaped[j++] = str[i];
            break;
        }
    }
    escaped[j++] = '"';
    escaped[j] = '\0';
    return escaped;
}

/* ==================== 帧解析状态机 ==================== */

static void frame_parser_init(mcp_frame_parser_t *p)
{
    AIRY_MEMSET(p, 0, sizeof(*p));
    p->state = MCP_FRAME_STATE_HEADER;
}

/**
 * @brief 在 buf[0..len) 中查找 "\r\n\r\n"
 * @return true 找到，*out 为分隔符之后的偏移
 */
static bool find_header_end(const char *buf, size_t len, size_t *out)
{
    if (len < 4)
        return false;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            *out = i + 4;
            return true;
        }
    }
    return false;
}

/**
 * @brief 从帧头解析 Content-Length（大小写不敏感，兼容多个头行）
 * @return 0 成功；-1 未找到或格式非法
 */
static int parse_content_length(const char *hdr, size_t hdr_len, size_t *out)
{
    size_t pos = 0;
    while (pos < hdr_len) {
        /* 找行尾 \r\n */
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
            while (v < line_end && n < sizeof(num_buf) - 1 &&
                   isdigit((unsigned char)hdr[v]))
                num_buf[n++] = hdr[v++];
            num_buf[n] = '\0';
            if (n == 0)
                return -1;
            *out = (size_t)strtoul(num_buf, NULL, 10);
            return 0;
        }
        pos = line_end + 2; /* 跳过 \r\n */
    }
    return -1;
}

static void frame_parser_destroy(mcp_frame_parser_t *p)
{
    if (!p)
        return;
    AIRY_FREE(p->buf);
    AIRY_FREE(p->header_text);
    AIRY_MEMSET(p, 0, sizeof(*p));
}

static int frame_parser_append(mcp_frame_parser_t *p, const char *data, size_t n)
{
    if (n == 0)
        return 0;
    if (p->len + n > MCP_CLIENT_MAX_MESSAGE_SIZE + MCP_CLIENT_HEADER_MAX) {
        LOG_WARN("mcp client: frame buffer overflow (len=%zu, n=%zu)", p->len, n);
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
 * @brief 尝试从累积缓冲中取出一帧
 * @return 0 取到完整帧（*out 为 body，AIRY_MALLOC）；
 *         1 数据不足，需继续读；
 *         <0 错误码
 */
static int frame_parser_take_frame(mcp_frame_parser_t *p, char **out)
{
    if (p->state == MCP_FRAME_STATE_HEADER) {
        size_t hdr_end = 0;
        if (!find_header_end(p->buf, p->len, &hdr_end))
            return 1;
        size_t clen = 0;
        if (parse_content_length(p->buf, hdr_end, &clen) != 0) {
            LOG_WARN("mcp client: frame header missing Content-Length");
            return MCP_CLIENT_ERR_FRAME;
        }
        if (clen == 0 || clen > MCP_CLIENT_MAX_MESSAGE_SIZE) {
            LOG_WARN("mcp client: invalid Content-Length %zu", clen);
            return MCP_CLIENT_ERR_FRAME;
        }
        /* 保存头部副本（HTTP 层解析状态码/Content-Type；stdio 层忽略） */
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

/* ==================== 写入（带超时） ==================== */

static int write_all_with_timeout(int fd, const char *data, size_t len, int timeout_ms)
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

/* ==================== stdio 传输 ==================== */

static int stdio_send_message(mcp_client_t *c, const char *body)
{
    size_t len = strlen(body);
    char header[64];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    int rc = write_all_with_timeout(c->stdio_write_fd, header, (size_t)hlen,
                                    MCP_CLIENT_DEFAULT_TIMEOUT_MS);
    if (rc != 0)
        return rc;
    return write_all_with_timeout(c->stdio_write_fd, body, len,
                                  MCP_CLIENT_DEFAULT_TIMEOUT_MS);
}

/**
 * @brief 从 stdio 读取一帧（poll 超时 + EOF/子进程退出检测）
 */
static int stdio_read_message(mcp_client_t *c, char **out, int timeout_ms)
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
            LOG_WARN("mcp client '%s': read timeout (%d ms)", c->name, timeout_ms);
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
            /* EOF：子进程退出或关闭了 stdout */
            LOG_WARN("mcp client '%s': stdio EOF, child process exited", c->name);
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

static char **copy_argv(char *const argv[])
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

static void free_argv(char **argv)
{
    if (!argv)
        return;
    for (size_t i = 0; argv[i]; i++)
        AIRY_FREE(argv[i]);
    AIRY_FREE(argv);
}

/* ==================== http 传输 ==================== */

/**
 * @brief 解析 http://host[:port]/path，默认端口 80、默认路径 /mcp
 */
static int parse_http_url(const char *url, char **host_out, uint16_t *port_out,
                          char **path_out)
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
        LOG_WARN("mcp client '%s': getaddrinfo failed for %s (%s)", c->name,
                 c->http_host, gai_strerror(grc));
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
 * @brief 从 SSE 流中提取第一个 "data:" 行的值
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
 * @brief 读取 HTTP 响应（复用帧状态机：header + Content-Length + body）
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
        /* Streamable HTTP 流式响应：提取 data: 行（基础支持单条消息） */
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

static int http_send_message(mcp_client_t *c, const char *body, char **out_body)
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
                        c->http_path, c->http_host, MCP_CLIENT_PROTOCOL_VERSION, blen,
                        body);
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

/* ==================== JSON-RPC 交换 ==================== */

/**
 * @brief 判断帧是否为指定 id 的响应
 * @return 1 匹配；0 不匹配（通知帧/其他 id）；<0 解析错误码
 */
static int response_id_matches(const char *frame, uint64_t expected)
{
    CJSON_PARSE_GUARD(root, frame, { return AIRY_ERR_PARSE_ERROR; });
    cJSON *rid = cJSON_GetObjectItem(root, "id");
    if (!rid)
        return 0; /* 通知帧 */
    if (cJSON_IsNumber(rid))
        return ((uint64_t)rid->valuedouble == expected) ? 1 : 0;
    if (cJSON_IsString(rid) && rid->valuestring)
        return (strtoull(rid->valuestring, NULL, 10) == expected) ? 1 : 0;
    return 0;
}

/**
 * @brief 发送 JSON-RPC 请求并（可选）等待匹配 id 的响应
 * @param expect_response true 时 out_response 输出响应帧文本（AIRY_MALLOC）
 */
static int client_rpc_exchange(mcp_client_t *c, const char *method, const char *params_json,
                               bool expect_response, char **out_response)
{
    uint64_t id = ++c->request_id;
    cJSON *req = cJSON_CreateObject();
    if (!req)
        return AIRY_ERR_OUT_OF_MEMORY;
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)id);
    cJSON_AddStringToObject(req, "method", method);
    cJSON *params = cJSON_Parse(params_json && params_json[0] ? params_json : "{}");
    cJSON_AddItemToObject(req, "params", params ? params : cJSON_CreateObject());
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_str)
        return AIRY_ERR_OUT_OF_MEMORY;

    int rc;
    if (c->transport == MCP_CLIENT_TRANSPORT_STDIO) {
        rc = stdio_send_message(c, req_str);
        AIRY_FREE(req_str);
        if (rc != 0)
            return rc;
        if (!expect_response)
            return 0;
        int64_t deadline = now_ms() + MCP_CLIENT_DEFAULT_TIMEOUT_MS;
        for (;;) {
            int64_t remain = deadline - now_ms();
            if (remain <= 0) {
                LOG_WARN("mcp client '%s': rpc %s timeout", c->name, method);
                return AIRY_ERR_TIMEOUT;
            }
            char *frame = NULL;
            rc = stdio_read_message(c, &frame, (int)remain);
            if (rc != 0)
                return rc;
            int m = response_id_matches(frame, id);
            if (m == 1) {
                *out_response = frame;
                return 0;
            }
            if (m < 0) {
                AIRY_FREE(frame);
                return m;
            }
            AIRY_FREE(frame); /* 通知/其他帧：丢弃继续读 */
        }
    } else if (c->transport == MCP_CLIENT_TRANSPORT_HTTP) {
        char *resp_body = NULL;
        rc = http_send_message(c, req_str, &resp_body);
        AIRY_FREE(req_str);
        if (rc != 0)
            return rc;
        if (!expect_response) {
            AIRY_FREE(resp_body);
            return 0;
        }
        if (response_id_matches(resp_body, id) < 0) {
            AIRY_FREE(resp_body);
            return AIRY_ERR_PARSE_ERROR;
        }
        *out_response = resp_body; /* 结果 JSON 原样返回 */
        return 0;
    }
    AIRY_FREE(req_str);
    return MCP_CLIENT_ERR_NOT_CONNECTED;
}

static int parse_initialize_response(mcp_client_t *c, const char *resp)
{
    CJSON_PARSE_GUARD(root, resp, { return AIRY_ERR_PARSE_ERROR; });
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        LOG_WARN("mcp client '%s': initialize rejected: %s", c->name,
                 msg && cJSON_IsString(msg) && msg->valuestring ? msg->valuestring
                                                                : "unknown error");
        return MCP_CLIENT_ERR_RPC_ERROR;
    }
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result))
        return AIRY_ERR_PARSE_ERROR;
    cJSON *si = cJSON_GetObjectItem(result, "serverInfo");
    if (cJSON_IsObject(si)) {
        cJSON *sn = cJSON_GetObjectItem(si, "name");
        if (cJSON_IsString(sn) && sn->valuestring) {
            c->server_name = AIRY_STRDUP(sn->valuestring);
            LOG_INFO("mcp client '%s': initialized, remote server=%s", c->name,
                     sn->valuestring);
        }
    } else {
        LOG_INFO("mcp client '%s': initialized", c->name);
    }
    return 0;
}

/**
 * @brief 首次使用时执行 initialize 握手（幂等）
 */
static int client_ensure_initialized(mcp_client_t *c)
{
    if (c->initialized)
        return 0;
    const char *params =
        "{\"protocolVersion\":\"2024-11-05\","
        "\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"agentrt-gateway\",\"version\":\"0.1.1\"}}";
    char *resp = NULL;
    int rc = client_rpc_exchange(c, "initialize", params, true, &resp);
    if (rc != 0)
        return rc;
    rc = parse_initialize_response(c, resp);
    AIRY_FREE(resp);
    if (rc != 0)
        return rc;
    c->initialized = true;
    /* notifications/initialized：通知无响应，发送失败仅告警 */
    rc = client_rpc_exchange(c, "notifications/initialized", "{}", false, NULL);
    if (rc != 0)
        LOG_WARN("mcp client '%s': failed to send initialized notification (rc=%d)",
                 c->name, rc);
    return 0;
}

/* ==================== 公开 API ==================== */

mcp_client_t *mcp_client_connect_stdio(const char *name, const char *command,
                                       char *const argv[])
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
        /* 子进程：stdin/stdout 接到管道，stderr 保持继承便于排查 */
        (void)dup2(to_child[0], STDIN_FILENO);
        (void)dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execvp(argv_copy[0], argv_copy);
        _exit(127); /* exec 失败 */
    }

    /* 父进程 */
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
    LOG_INFO("mcp client '%s': stdio connected (pid=%d, command=%s)", name, (int)pid,
             command);
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
    LOG_INFO("mcp client '%s': http endpoint http://%s:%u%s", name, host,
             (unsigned)port, path);
    return c;
}

int mcp_client_list_tools(mcp_client_t *c, mcp_client_tool_list_t *out)
{
    if (!c || !out)
        return AIRY_ERR_INVALID_PARAM;
    if (c->transport == MCP_CLIENT_TRANSPORT_NONE)
        return MCP_CLIENT_ERR_NOT_CONNECTED;
    AIRY_MEMSET(out, 0, sizeof(*out));

    int rc = client_ensure_initialized(c);
    if (rc != 0)
        return rc;

    char *resp = NULL;
    rc = client_rpc_exchange(c, "tools/list", "{}", true, &resp);
    if (rc != 0)
        return rc;

    CJSON_PARSE_GUARD(root, resp, {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    });
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        LOG_WARN("mcp client '%s': tools/list failed: %s", c->name,
                 msg && cJSON_IsString(msg) && msg->valuestring ? msg->valuestring
                                                                : "unknown error");
        AIRY_FREE(resp);
        return MCP_CLIENT_ERR_RPC_ERROR;
    }
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    }
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (!cJSON_IsArray(tools)) {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    }

    int n = cJSON_GetArraySize(tools);
    if (n <= 0) {
        AIRY_FREE(resp);
        return 0; /* 外部 server 无工具 */
    }
    mcp_client_tool_t *arr = AIRY_CALLOC((size_t)n, sizeof(mcp_client_tool_t));
    if (!arr) {
        AIRY_FREE(resp);
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    size_t filled = 0;
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_GetArrayItem(tools, i);
        cJSON *jname = cJSON_GetObjectItem(t, "name");
        if (!cJSON_IsString(jname) || !jname->valuestring)
            continue;
        arr[filled].name = AIRY_STRDUP(jname->valuestring);
        if (!arr[filled].name)
            continue;
        cJSON *jdesc = cJSON_GetObjectItem(t, "description");
        if (cJSON_IsString(jdesc) && jdesc->valuestring)
            arr[filled].description = AIRY_STRDUP(jdesc->valuestring);
        cJSON *jschema = cJSON_GetObjectItem(t, "inputSchema");
        if (cJSON_IsObject(jschema))
            arr[filled].input_schema_json = cJSON_PrintUnformatted(jschema);
        filled++;
    }
    AIRY_FREE(resp);

    if (filled == 0) {
        mcp_client_tool_list_t tmp;
        tmp.tools = arr;
        tmp.count = 0;
        mcp_client_tool_list_free(&tmp);
        return 0;
    }
    out->tools = arr;
    out->count = filled;
    return 0;
}

int mcp_client_call_tool(mcp_client_t *c, const char *name, const char *arguments_json,
                         char **result_json)
{
    if (!c || !name || !result_json)
        return AIRY_ERR_INVALID_PARAM;
    if (c->transport == MCP_CLIENT_TRANSPORT_NONE)
        return MCP_CLIENT_ERR_NOT_CONNECTED;
    *result_json = NULL;

    int rc = client_ensure_initialized(c);
    if (rc != 0)
        return rc;

    cJSON *params = cJSON_CreateObject();
    if (!params)
        return AIRY_ERR_OUT_OF_MEMORY;
    cJSON_AddStringToObject(params, "name", name);
    cJSON *args = cJSON_Parse(arguments_json && arguments_json[0] ? arguments_json : "{}");
    cJSON_AddItemToObject(params, "arguments", args ? args : cJSON_CreateObject());
    char *params_str = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (!params_str)
        return AIRY_ERR_OUT_OF_MEMORY;

    rc = client_rpc_exchange(c, "tools/call", params_str, true, result_json);
    AIRY_FREE(params_str);
    return rc;
}

int mcp_client_extract_text(const char *response_json, char **text_json)
{
    *text_json = NULL;
    if (!response_json)
        return AIRY_ERR_INVALID_PARAM;
    CJSON_PARSE_GUARD(root, response_json, { return AIRY_ERR_PARSE_ERROR; });

    /* 顶层 JSON-RPC error：输出 message（gateway 侧嵌入 text 返回） */
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        const char *raw = (cJSON_IsString(msg) && msg->valuestring)
                              ? msg->valuestring
                              : "MCP RPC error";
        *text_json = json_string_escape(raw);
        return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result))
        return AIRY_ERR_PARSE_ERROR;
    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        cJSON *text = cJSON_GetObjectItem(first, "text");
        if (cJSON_IsString(text) && text->valuestring) {
            *text_json = json_string_escape(text->valuestring);
            return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
        }
        /* 非 text content（image/resource）：原样序列化该节点 */
        *text_json = cJSON_PrintUnformatted(first);
        return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
    }
    /* 无 content：序列化整个 result 供上层排查 */
    *text_json = cJSON_PrintUnformatted(result);
    return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
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
            /* 先优雅 SIGTERM，2s 内未退出再 SIGKILL */
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
