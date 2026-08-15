// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_cb.c
 * @brief A2A v0.3 protocol adapter callback domain (codec/connection/transport/request handling).
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <stdio.h>
#include <string.h>

#include "logging.h"

/* A2A v03 protocol message frame header constants */
#define A2A_V03_FRAME_MAGIC "A2A/0.3"
#define A2A_V03_FRAME_HDR_SEP "\r\n"
#define A2A_V03_FRAME_END "\r\n\r\n"
#define A2A_V03_FRAME_HDR_MAX 256

int a2a_adapter_init_cb(void *context)
{
    if (!context)
        return AIRY_ENOMEM;
    return 0;
}
int a2a_adapter_destroy_cb(void *context)
{
    a2a_v03_context_destroy((a2a_v03_context_t *)context);
    return 0;
}
int a2a_adapter_encode_cb(void *c, const void *m, void **o, size_t *s)
{
    if (!c || !m || !o || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_encode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    const char *msg = (const char *)m;
    size_t len = strlen(msg) + 1;
    char *buf = (char *)AIRY_MALLOC(len);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "strlen: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(buf, msg, len);
    *o = buf;
    *s = len;
    return 0;
}
int a2a_adapter_decode_cb(void *c, const void *d, size_t s, void *o)
{
    if (!c || !d || !o || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_decode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    __builtin_memcpy(o, d, s);
    return 0;
}
int a2a_adapter_connect_cb(void *c, const char *e)
{
    if (!c) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_connect_cb: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    adapter->connected = true;
    AIRY_LOG_DEBUG("a2a_adapter_connect_cb: connected to %s", e ? e : "(unknown)");
    (void)e;
    return 0;
}
int a2a_adapter_disconnect_cb(void *c)
{
    if (!c) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_disconnect_cb: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    adapter->connected = false;
    AIRY_LOG_DEBUG("a2a_adapter_disconnect_cb: disconnected");
    return 0;
}
int a2a_adapter_is_connected_cb(void *c)
{
    if (!c)
        return 0;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    return adapter->connected ? 1 : 0;
}
int a2a_adapter_send_cb(void *c, const void *d, size_t s)
{
    if (!c || !d || s == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "a2a_adapter_send_cb: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (!adapter->connected || !adapter->transport_write) {
        AIRY_LOG_WARN("send failed: not connected or no transport, connected=%d, transport_write=%p",
                 adapter->connected, (void *)(uintptr_t)adapter->transport_write);

        airy_err_push_ex(AIRY_ENOTCONN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_send_cb: not connected or no transport");
        return AIRY_ENOTCONN;
    }

    /* Format A2A v03 protocol message frame:
     *   A2A/0.3\r\n
     *   Content-Length: <size>\r\n
     *   Content-Type: application/json\r\n
     *   \r\n
     *   <payload>
     */
    char header[A2A_V03_FRAME_HDR_MAX];
    int hdr_len = snprintf(header, sizeof(header),
                           A2A_V03_FRAME_MAGIC A2A_V03_FRAME_HDR_SEP
                           "Content-Length: %zu" A2A_V03_FRAME_HDR_SEP
                           "Content-Type: application/json" A2A_V03_FRAME_END,
                           s);
    if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(header)) {
        airy_err_push_ex(AIRY_ERR_OVERFLOW, __FILE__, __LINE__, __func__, "header overflow");
        return AIRY_ERR_OVERFLOW;
    }

    AIRY_LOG_DEBUG("a2a_adapter_send_cb: sending %zu bytes (frame hdr=%d)", s, hdr_len);

    /* Send header */
    int rc = adapter->transport_write(adapter->transport_ctx, header, (size_t)hdr_len);
    if (rc != AIRY_OK) {
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "transport write header failed");
        return AIRY_ERR_IO;
    }

    /* Send payload */
    rc = adapter->transport_write(adapter->transport_ctx, d, s);
    if (rc != AIRY_OK) {
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "transport write payload failed");
        return AIRY_ERR_IO;
    }

    adapter->bytes_sent += (uint64_t)s + (uint64_t)hdr_len;
    adapter->messages_sent++;

    AIRY_LOG_DEBUG("a2a_adapter_send_cb: sent message #%llu (%zu bytes payload)",
              (unsigned long long)adapter->messages_sent, s);

    return AIRY_OK;
}
int a2a_adapter_receive_cb(void *c, void **d, size_t *s, uint32_t t)
{
    if (!c || !d || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_receive_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    (void)t;
    *d = NULL;
    *s = 0;
    return AIRY_EINVAL;
}
int a2a_adapter_handle_request_cb(void *c, const void *r, void **rp)
{
    if (!c || !r) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_handle_request_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (rp)
        *rp = NULL;
    return a2a_v03_route_request((a2a_v03_context_t *)c, (const char *)r, NULL, (char **)rp);
}
int a2a_adapter_get_version_cb(void *c, char *b, size_t s)
{
    (void)c;
    snprintf(b, s, "0.3.0");
    return 0;
}
uint32_t a2a_adapter_capabilities_cb(void *c)
{
    (void)c;
    return A2A_CAP_TASK_EXECUTION | A2A_CAP_STREAMING | A2A_CAP_NEGOTIATION;
}
int a2a_adapter_get_stats_cb(void *c, char *b, size_t s)
{
    if (!c || !b || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_get_stats_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *a = (struct a2a_v03_adapter_s *)c;
    snprintf(b, s, "{\"agents\":%zu,\"tasks\":%zu}", a->agent_count, a->task_count);
    return 0;
}
