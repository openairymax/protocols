// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_utils.c
 * @brief OpenAI enterprise adapter utility domain (hashing/JSON escaping/token estimation/latency stats/curl API calls).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/error.h"
#include "error.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AIRY_HAS_CURL
#include <curl/curl.h>
#endif

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#endif

/* ============================================================================
 * Internal: Response Generation & Token Estimation
 * ============================================================================ */

uint64_t openai_fnv1a_hash(const char *str)
{
    uint64_t hash = OPENAI_FNV_OFFSET;
    if (!str)
        return hash;
    for (; *str; str++) {
        hash ^= (unsigned char)*str;
        hash *= OPENAI_FNV_PRIME;
    }
    return hash;
}

void json_escape_string(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0)
        return;
    size_t si = 0, di = 0;
    while (src[si] && di < dst_size - 1) {
        switch (src[si]) {
        case '"':
            if (di + 1 < dst_size - 1) {
                dst[di++] = '\\';
                dst[di++] = '"';
            }
            break;
        case '\\':
            if (di + 1 < dst_size - 1) {
                dst[di++] = '\\';
                dst[di++] = '\\';
            }
            break;
        case '\n':
            if (di + 1 < dst_size - 1) {
                dst[di++] = '\\';
                dst[di++] = 'n';
            }
            break;
        case '\r':
            if (di + 1 < dst_size - 1) {
                dst[di++] = '\\';
                dst[di++] = 'r';
            }
            break;
        case '\t':
            if (di + 1 < dst_size - 1) {
                dst[di++] = '\\';
                dst[di++] = 't';
            }
            break;
        default:
            dst[di++] = src[si];
            break;
        }
        si++;
    }
    dst[di] = '\0';
}

int openai_estimate_tokens(const char *text)
{
    if (!text || !*text)
        return 0;
    int count = 0;
    bool in_word = false;
    for (const char *p = text; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '_' || (*p & 0x80)) {
            if (!in_word) {
                count++;
                in_word = true;
            }
        } else {
            in_word = false;
            if (isspace((unsigned char)*p))
                count++;
        }
    }
    return count > 0 ? count : 1;
}

void openai_record_latency(struct openai_enterprise_adapter_s *adapter, double latency_ms)
{
    adapter->stats_total_latency_ms += latency_ms;
    if (latency_ms < adapter->stats_min_latency_ms)
        adapter->stats_min_latency_ms = latency_ms;
    if (latency_ms > adapter->stats_max_latency_ms)
        adapter->stats_max_latency_ms = latency_ms;
    adapter->stats_latency_samples[adapter->stats_latency_index] = latency_ms;
    adapter->stats_latency_index = (adapter->stats_latency_index + 1) % OPENAI_STATS_HISTORY_SIZE;
    if (adapter->stats_latency_count < OPENAI_STATS_HISTORY_SIZE)
        adapter->stats_latency_count++;
}

#ifdef AIRY_HAS_CURL
typedef struct {
    char *data;
    size_t size;
} openai_curl_buffer_t;

static size_t openai_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    openai_curl_buffer_t *buf = (openai_curl_buffer_t *)userdata;
    size_t total = size * nmemb;
    char *new_data = (char *)AIRY_REALLOC(buf->data, buf->size + total + 1);
    if (!new_data)
        return 0;
    buf->data = new_data;
    __builtin_memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

int openai_api_call(const char *api_key, const char *base_url, const char *endpoint,
                    const char *request_json, char *out_buf, size_t buf_len)
{
    if (!api_key || !request_json || !out_buf) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai_api_call: failed");
        return AIRY_ERR_UNKNOWN;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }

    openai_curl_buffer_t response_buf = {.data = NULL, .size = 0};

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", base_url ? base_url : "https://api.openai.com/v1",
             endpoint ? endpoint : "/chat/completions");

    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, openai_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        AIRY_FREE(response_buf.data);
        return AIRY_EIO;
    }

    if (http_code == 200 && response_buf.data) {
        size_t copy_len = response_buf.size;
        if (copy_len >= buf_len)
            copy_len = buf_len - 1;
        __builtin_memcpy(out_buf, response_buf.data, copy_len);
        out_buf[copy_len] = '\0';
        AIRY_FREE(response_buf.data);
        return (int)copy_len;
    }

    AIRY_FREE(response_buf.data);
    return AIRY_EINVAL;
}

int openai_parse_chat_response(const char *json_str, char *content_out, size_t content_len,
                               openai_usage_t *usage)
{
    if (!json_str || !content_out) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_parse_chat_response: parse error");
        return AIRY_ERR_UNKNOWN;
    }

    CJSON_PARSE_GUARD(root, json_str, {
        airy_err_push_ex(AIRY_ERR_NOT_FOUND, __FILE__, __LINE__, __func__, "openai: not found");
        return AIRY_ERR_NOT_FOUND;
    });

    int result = -3;
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first, "message");
        if (message) {
            cJSON *content = cJSON_GetObjectItem(message, "content");
            if (content && content->valuestring) {
                AIRY_STRNCPY_TERM(content_out, content->valuestring, content_len);
                content_out[content_len - 1] = '\0';
                result = 0;
            }
        }
    }

    if (usage) {
        cJSON *usage_obj = cJSON_GetObjectItem(root, "usage");
        if (usage_obj) {
            cJSON *pt = cJSON_GetObjectItem(usage_obj, "prompt_tokens");
            cJSON *ct = cJSON_GetObjectItem(usage_obj, "completion_tokens");
            cJSON *tt = cJSON_GetObjectItem(usage_obj, "total_tokens");
            usage->prompt_tokens = pt ? pt->valueint : 0;
            usage->completion_tokens = ct ? ct->valueint : 0;
            usage->total_tokens = tt ? tt->valueint : 0;
        }
    }

    return result;
}
#endif
