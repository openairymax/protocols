// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file china_eco_llm.c
 * @brief LLM provider bridge for china_eco adapter.
 *
 * Extracted from china_eco_adapter.c — handles OpenAI-compatible
 * chat-completion HTTP calls for all domestic LLM providers
 * (Bailian / Wenxin / DashScope / Zhipu / MiniMax / Moonshot /
 * DeepSeek / Qwen).
 */

#include "china_eco_llm.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>
#endif
#ifdef AIRY_HAS_CURL
#include <curl/curl.h>
#endif

/* ------------------------------------------------------------------ */
/* Provider tables                                                     */
/* ------------------------------------------------------------------ */

const char *g_provider_api_urls[] = {
    [CHINA_ECO_PROVIDER_BAILIAN] = "https://dashscope.aliyuncs.com/compatible-mode/v1",
    [CHINA_ECO_PROVIDER_WENXIN] = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1",
    [CHINA_ECO_PROVIDER_DASHSCOPE] = "https://dashscope.aliyuncs.com/api/v1",
    [CHINA_ECO_PROVIDER_ZHIPU] = "https://open.bigmodel.cn/api/paas/v4",
    [CHINA_ECO_PROVIDER_MINIMAX] = "https://api.minimax.chat/v1",
    [CHINA_ECO_PROVIDER_MOONSHOT] = "https://api.moonshot.cn/v1",
    [CHINA_ECO_PROVIDER_DEEPSEEK] = "https://api.deepseek.com/v1",
    [CHINA_ECO_PROVIDER_QWEN] = "https://dashscope.aliyuncs.com/compatible-mode/v1"};

const char *g_provider_names[] = {
    [CHINA_ECO_PROVIDER_BAILIAN] = "bailian",     [CHINA_ECO_PROVIDER_WENXIN] = "wenxin",
    [CHINA_ECO_PROVIDER_DASHSCOPE] = "dashscope", [CHINA_ECO_PROVIDER_ZHIPU] = "zhipu",
    [CHINA_ECO_PROVIDER_MINIMAX] = "minimax",     [CHINA_ECO_PROVIDER_MOONSHOT] = "moonshot",
    [CHINA_ECO_PROVIDER_DEEPSEEK] = "deepseek",   [CHINA_ECO_PROVIDER_QWEN] = "qwen"};

/* ------------------------------------------------------------------ */
/* curl write callback                                                 */
/* ------------------------------------------------------------------ */

#ifdef AIRY_HAS_CURL
typedef struct {
    char *data;
    size_t size;
} china_eco_curl_buf_t;

static size_t china_eco_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    china_eco_curl_buf_t *buf = (china_eco_curl_buf_t *)userdata;
    char *new_data = (char *)AIRY_REALLOC(buf->data, buf->size + total + 1);
    if (!new_data)
        return 0;
    __builtin_memcpy(new_data + buf->size, ptr, total);
    buf->data = new_data;
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}
#endif

/* ------------------------------------------------------------------ */
/* HTTP chat completion                                                */
/* ------------------------------------------------------------------ */

int china_eco_llm_chat_http(const china_eco_llm_provider_t *provider,
                             const char *api_base_url, const char *model_id,
                             const char *messages_json, char *response, size_t resp_size,
                             uint64_t *prompt_tokens, uint64_t *completion_tokens)
{
#if defined(AIRY_HAS_CURL) && defined(AIRY_HAS_CJSON)
    cJSON *msgs = cJSON_Parse(messages_json);
    if (!msgs || !cJSON_IsArray(msgs)) {
        if (msgs)
            cJSON_Delete(msgs);
        airy_err_push_ex(AIRY_ERR_PARSE_ERROR, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: messages_json is not a JSON array");
        return AIRY_ERR_PARSE_ERROR;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", model_id);
    cJSON_AddItemToObject(req, "messages", msgs);
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_str) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: request serialization failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/chat/completions",
             api_base_url && api_base_url[0] ? api_base_url : "https://api.openai.com/v1");

    CURL *curl = curl_easy_init();
    if (!curl) {
        AIRY_FREE(req_str);
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: curl init failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    china_eco_curl_buf_t response_buf = {0};
    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", provider->api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, china_eco_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    AIRY_FREE(req_str);

    if (res != CURLE_OK) {
        AIRY_FREE(response_buf.data);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: curl transfer failed (code %d)", (int)res);
        return AIRY_ERR_IO;
    }
    if (http_code != 200 || !response_buf.data) {
        AIRY_FREE(response_buf.data);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: provider returned HTTP %ld", http_code);
        return AIRY_ERR_IO;
    }

    cJSON *root = cJSON_Parse(response_buf.data);
    AIRY_FREE(response_buf.data);
    if (!root) {
        airy_err_push_ex(AIRY_ERR_PARSE_ERROR, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: invalid response JSON");
        return AIRY_ERR_PARSE_ERROR;
    }

    const char *content = NULL;
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
        if (message) {
            cJSON *content_item = cJSON_GetObjectItem(message, "content");
            if (content_item && content_item->valuestring)
                content = content_item->valuestring;
        }
    }

    int rc = AIRY_ERR_IO;
    if (content && content[0]) {
        size_t clen = strlen(content);
        if (clen >= resp_size)
            clen = resp_size - 1;
        __builtin_memcpy(response, content, clen);
        response[clen] = '\0';
        rc = (int)clen;
    }

    if (prompt_tokens || completion_tokens) {
        cJSON *usage = cJSON_GetObjectItem(root, "usage");
        if (usage) {
            cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
            cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
            if (prompt_tokens)
                *prompt_tokens = pt && pt->valueint > 0 ? (uint64_t)pt->valueint : 0;
            if (completion_tokens)
                *completion_tokens = ct && ct->valueint > 0 ? (uint64_t)ct->valueint : 0;
        }
    }

    cJSON_Delete(root);
    return rc;
#else
    (void)provider;
    (void)api_base_url;
    (void)model_id;
    (void)messages_json;
    (void)response;
    (void)resp_size;
    (void)prompt_tokens;
    (void)completion_tokens;
    airy_err_push_ex(AIRY_ERR_NOT_SUPPORTED, __FILE__, __LINE__, __func__,
                     "china_eco_llm_chat_http: build without curl/cJSON");
    return AIRY_ERR_NOT_SUPPORTED;
#endif
}
