// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file proto_http.c
 * @brief curl JSON POST 传输机制（写回调 + 仪式序列单点实现）。
 */

#include "proto_http.h"

#include "airy_memory.h"

#ifdef AIRY_HAS_CURL
#include <curl/curl.h>

typedef struct {
    char *data;
    size_t size;
} proto_http_body_t;

static size_t proto_http_write_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
    proto_http_body_t *buf = (proto_http_body_t *)ud;
    size_t total = size * nmemb;
    char *grown = (char *)AIRY_REALLOC(buf->data, buf->size + total + 1);
    if (!grown)
        return 0;
    buf->data = grown;
    AIRY_MEMCPY(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

proto_http_result_t proto_http_post(const char *url, const char *const *hdrs,
                                    const char *body, char **out_body, long *out_code)
{
    if (out_body)
        *out_body = NULL;
    if (out_code)
        *out_code = 0;

    CURL *curl = curl_easy_init();
    if (!curl)
        return PROTO_HTTP_E_INIT;

    proto_http_body_t buf = {NULL, 0};
    struct curl_slist *list = NULL;
    for (size_t i = 0; hdrs && hdrs[i]; i++)
        list = curl_slist_append(list, hdrs[i]);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, proto_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        AIRY_FREE(buf.data);
        return PROTO_HTTP_E_TRANSPORT;
    }
    if (out_code)
        *out_code = http_code;
    *out_body = buf.data;
    return PROTO_HTTP_OK;
}

#else /* !AIRY_HAS_CURL */

proto_http_result_t proto_http_post(const char *url, const char *const *hdrs,
                                    const char *body, char **out_body, long *out_code)
{
    (void)url;
    (void)hdrs;
    (void)body;
    if (out_body)
        *out_body = NULL;
    if (out_code)
        *out_code = 0;
    return PROTO_HTTP_E_INIT;
}

#endif /* AIRY_HAS_CURL */
