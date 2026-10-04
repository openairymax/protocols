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
#include "proto_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>
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

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", provider->api_key);
    const char *hdrs[] = {auth_header, "Content-Type: application/json", NULL};

    long http_code = 0;
    char *resp = NULL;
    proto_http_result_t tr = proto_http_post(url, hdrs, req_str, &resp, &http_code);
    AIRY_FREE(req_str);

    if (tr == PROTO_HTTP_E_INIT) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: curl init failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    if (tr == PROTO_HTTP_E_TRANSPORT) {
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: curl transfer failed");
        return AIRY_ERR_IO;
    }
    if (http_code != 200 || !resp) {
        AIRY_FREE(resp);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "china_eco_llm_chat_http: provider returned HTTP %ld", http_code);
        return AIRY_ERR_IO;
    }

    cJSON *root = cJSON_Parse(resp);
    AIRY_FREE(resp);
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
