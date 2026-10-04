// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_utils.c
 * @brief OpenAI enterprise adapter utility domain (hashing/JSON escaping/latency stats/curl API calls).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "proto_http.h"
#include "../../../../commons/utils/error/error.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#endif

/* ============================================================================
 * Internal: Hashing, JSON Escaping & Latency Stats
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

void oai_record_latency(struct openai_enterprise_adapter_s *adapter, double latency_ms)
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
int openai_api_call(const char *api_key, const char *base_url, const char *endpoint,
                    const char *request_json, char *out_buf, size_t buf_len)
{
    if (!api_key || !request_json || !out_buf) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai_api_call: failed");
        return AIRY_ERR_UNKNOWN;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", base_url ? base_url : "https://api.openai.com/v1",
             endpoint ? endpoint : "/chat/completions");

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    const char *hdrs[] = {auth_header, "Content-Type: application/json", NULL};

    long http_code = 0;
    char *resp = NULL;
    proto_http_result_t tr = proto_http_post(url, hdrs, request_json, &resp, &http_code);
    if (tr == PROTO_HTTP_E_INIT) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (tr == PROTO_HTTP_E_TRANSPORT)
        return AIRY_EIO;

    if (http_code == 200 && resp) {
        size_t copy_len = strlen(resp);
        if (copy_len >= buf_len)
            copy_len = buf_len - 1;
        AIRY_MEMCPY(out_buf, resp, copy_len);
        out_buf[copy_len] = '\0';
        AIRY_FREE(resp);
        return (int)copy_len;
    }

    AIRY_FREE(resp);
    return AIRY_EINVAL;
}

int oai_parse_chat_resp(const char *json_str, char *content_out, size_t content_len,
                               openai_usage_t *usage)
{
    if (!json_str || !content_out) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "oai_parse_chat_resp: parse error");
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
