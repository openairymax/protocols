// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file claude_adapter_http.c
 * @brief Claude adapter HTTPS API request building, response parsing and streaming output domain.
 *
 * Single responsibility: build Claude Messages API request JSON, send it via
 * curl, parse the response content_blocks/usage, and split streaming events
 * by block.
 */

#define LOG_TAG "claude_adapter"

#include "claude_adapter.h"
#include "claude_adapter_internal.h"

#include "error.h"
#include "logging.h"
#include "airy_memory.h"
#include "protocol_transformers.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef AIRY_HAS_CURL
#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#include <curl/curl.h>

typedef struct {
    char *data;
    size_t size;
} claude_curl_buffer_t;

static size_t claude_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    claude_curl_buffer_t *buf = (claude_curl_buffer_t *)userdata;
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

int claude_api_call(const char *api_key, const char *base_url, const char *request_json,
                    char *out_buf, size_t buf_len)
{
    if (!api_key || !request_json || !out_buf)
        return AIRY_ERR_NULL_POINTER;

    CURL *curl = curl_easy_init();
    if (!curl)
        return AIRY_ERR_SYS_RESOURCE;

    claude_curl_buffer_t response_buf = {.data = NULL, .size = 0};

    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", base_url ? base_url : "https://api.anthropic.com");

    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, claude_curl_write_cb);
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
        return AIRY_ERR_IO;
    }

    if (http_code == 200 && response_buf.data) {

        do {
            CJSON_PARSE_GUARD(root, response_buf.data, { break; });
            cJSON *content_arr = cJSON_GetObjectItem(root, "content");
            if (content_arr && cJSON_IsArray(content_arr)) {
                cJSON *first = cJSON_GetArrayItem(content_arr, 0);
                if (first) {
                    cJSON *text = cJSON_GetObjectItem(first, "text");
                    if (text && text->valuestring) {
                        snprintf(out_buf, buf_len, "%s", text->valuestring);

                        AIRY_FREE(response_buf.data);
                        return (int)strlen(out_buf);
                    }
                }
            }

        } while (0);
    }

    AIRY_FREE(response_buf.data);
    return AIRY_ERR_LLM_PROVIDER_FAIL;
}

#endif

int claude_generate_response(const char *user_msg, const char *system_ctx, char *out_buf,
                             size_t buf_len)
{
#ifndef AIRY_HAS_CURL
    (void)user_msg;
    (void)system_ctx;
    if (out_buf && buf_len > 0)
        out_buf[0] = '\0';
    return -ENOSYS;
#else
    if (!user_msg || !out_buf || buf_len == 0)
        return AIRY_ERR_NULL_POINTER;

    if (!g_claude_proto_context)
        return AIRY_ERR_INVALID_PARAM;
    claude_adapter_context_t *ctx = (claude_adapter_context_t *)g_claude_proto_context;
    if (!ctx->config.api_key || !ctx->config.api_key[0])
        return AIRY_ERR_INVALID_PARAM;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", claude_model_id_to_api_name(ctx->config.default_model));
    cJSON_AddNumberToObject(req, "max_tokens", ctx->config.max_tokens);
    cJSON *msgs = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_CreateArray();
    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", user_msg);
    cJSON_AddItemToArray(content, text_obj);
    cJSON_AddItemToObject(msg, "content", content);
    cJSON_AddItemToArray(msgs, msg);
    cJSON_AddItemToObject(req, "messages", msgs);
    if (system_ctx && system_ctx[0])
        cJSON_AddStringToObject(req, "system", system_ctx);

    char *req_json = cJSON_PrintUnformatted(req);
    int result =
        claude_api_call(ctx->config.api_key, ctx->config.base_url, req_json, out_buf, buf_len);
    AIRY_FREE(req_json);
    cJSON_Delete(req);
    return result > 0 ? result : AIRY_ERR_LLM_PROVIDER_FAIL;
#endif
}

int claude_messages_create(claude_adapter_context_t *ctx, const claude_message_t *messages,
                           size_t message_count, const claude_tool_def_t *tools, size_t tool_count,
                           const char *system_prompt, claude_response_t *response)
{
    if (!ctx || !response)
        return AIRY_ERR_NULL_POINTER;
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    ctx->total_requests++;

    AIRY_MEMSET(response, 0, sizeof(claude_response_t));

    if (ctx->message_handler) {
        const char *model_name = claude_model_id_to_api_name(ctx->config.default_model);
        int ret = ctx->message_handler(model_name, messages, message_count, tools, tool_count,
                                       system_prompt ? system_prompt : ctx->config.system_prompt,
                                       response, ctx->message_handler_data);
        if (ret == 0) {
            ctx->total_tokens_in += response->input_tokens;
            ctx->total_tokens_out += response->output_tokens;
            for (size_t i = 0; i < response->block_count; i++) {
                if (response->content_blocks[i].type &&
                    strcmp(response->content_blocks[i].type, "tool_use") == 0)
                    ctx->total_tool_calls++;
            }
        }
        return ret;
    }

#ifdef AIRY_HAS_CURL
    if (!ctx->config.api_key || !ctx->config.api_key[0])
        return AIRY_ERR_UNKNOWN;

    const char *model_name = claude_model_id_to_api_name(ctx->config.default_model);
    const char *user_content = "";
    const char *sys_ctx = system_prompt ? system_prompt : ctx->config.system_prompt;

    for (size_t i = message_count; i > 0; i--) {
        if (messages[i - 1].role == CLAUDE_ROLE_USER && messages[i - 1].content) {
            user_content = messages[i - 1].content;
            break;
        }
    }
    (void)user_content;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", model_name);
    cJSON_AddNumberToObject(req, "max_tokens", ctx->config.max_tokens);
    cJSON *msgs = cJSON_CreateArray();
    for (size_t i = 0; i < message_count && i < 128; i++) {
        cJSON *msg = cJSON_CreateObject();
        const char *role = "user";
        if (messages[i].role == CLAUDE_ROLE_ASSISTANT)
            role = "assistant";
        cJSON_AddStringToObject(msg, "role", role);
        cJSON *content_arr = cJSON_CreateArray();
        cJSON *text_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(text_obj, "type", "text");
        cJSON_AddStringToObject(text_obj, "text", messages[i].content ? messages[i].content : "");
        cJSON_AddItemToArray(content_arr, text_obj);
        cJSON_AddItemToObject(msg, "content", content_arr);
        cJSON_AddItemToArray(msgs, msg);
    }
    cJSON_AddItemToObject(req, "messages", msgs);
    if (sys_ctx && sys_ctx[0])
        cJSON_AddStringToObject(req, "system", sys_ctx);

    char *req_json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char api_response[8192];
    AIRY_MEMSET(api_response, 0, sizeof(api_response));
    int api_result = claude_api_call(ctx->config.api_key, ctx->config.base_url, req_json,
                                     api_response, sizeof(api_response));
    AIRY_FREE(req_json);

    if (api_result <= 0)
        return AIRY_ERR_UNKNOWN;

    CJSON_PARSE_GUARD(root, api_response, { return AIRY_ERR_UNKNOWN; });

    static uint32_t msg_counter = 0;
    msg_counter++;
    char resp_id[64];
    snprintf(resp_id, sizeof(resp_id), "msg_%08x", msg_counter);
    response->id = AIRY_STRDUP(resp_id);
    response->model = AIRY_STRDUP(model_name);
    response->role = CLAUDE_ROLE_ASSISTANT;
    response->stop_reason = CLAUDE_STOP_END_TURN;

    cJSON *content_arr = cJSON_GetObjectItem(root, "content");
    int block_count = 0;
    if (content_arr && cJSON_IsArray(content_arr))
        block_count = cJSON_GetArraySize(content_arr);

    if (block_count > 0) {
        response->content_blocks =
            (claude_content_block_t *)AIRY_CALLOC((size_t)block_count,
                                                  sizeof(claude_content_block_t));
        response->block_count = (size_t)block_count;
        for (int i = 0; i < block_count; i++) {
            cJSON *block = cJSON_GetArrayItem(content_arr, i);
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (type && type->valuestring && strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                response->content_blocks[i].type = AIRY_STRDUP("text");
                response->content_blocks[i].content.text =
                    AIRY_STRDUP(text && text->valuestring ? text->valuestring : "");
            } else if (type && type->valuestring && strcmp(type->valuestring, "tool_use") == 0) {
                cJSON *id = cJSON_GetObjectItem(block, "id");
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *input = cJSON_GetObjectItem(block, "input");
                response->content_blocks[i].type = AIRY_STRDUP("tool_use");
                response->content_blocks[i].content.tool_use.id =
                    AIRY_STRDUP(id && id->valuestring ? id->valuestring : "");
                response->content_blocks[i].content.tool_use.name =
                    AIRY_STRDUP(name && name->valuestring ? name->valuestring : "");
                char *input_str = input ? cJSON_PrintUnformatted(input) : AIRY_STRDUP("{}");
                response->content_blocks[i].content.tool_use.input_json = input_str;
                ctx->total_tool_calls++;
            }
        }
    }

    cJSON *usage_obj = cJSON_GetObjectItem(root, "usage");
    if (usage_obj) {
        cJSON *it = cJSON_GetObjectItem(usage_obj, "input_tokens");
        cJSON *ot = cJSON_GetObjectItem(usage_obj, "output_tokens");
        response->input_tokens = it ? it->valueint : 0;
        response->output_tokens = ot ? ot->valueint : 0;
    }

    ctx->total_tokens_in += response->input_tokens;
    ctx->total_tokens_out += response->output_tokens;
    return 0;
#else
    return -ENOSYS;
#endif
}

int claude_messages_stream(claude_adapter_context_t *ctx, const claude_message_t *messages,
                           size_t message_count, const claude_tool_def_t *tools, size_t tool_count,
                           const char *system_prompt, claude_stream_handler_t handler,
                           void *user_data)
{
    if (!ctx || !handler)
        return AIRY_ERR_NULL_POINTER;
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    ctx->total_requests++;

    const char *user_content = "";
    const char *sys_ctx = system_prompt ? system_prompt : ctx->config.system_prompt;

    for (size_t i = message_count; i > 0; i--) {
        if (messages[i - 1].role == CLAUDE_ROLE_USER && messages[i - 1].content) {
            user_content = messages[i - 1].content;
            break;
        }
    }

    char full_response[CLAUDE_MAX_RESPONSE_LEN];
    AIRY_MEMSET(full_response, 0, sizeof(full_response));
    int gen_result =
        claude_generate_response(user_content, sys_ctx, full_response, sizeof(full_response));
    if (gen_result < 0)
        return gen_result;

    size_t resp_len = strlen(full_response);
    size_t pos = 0;
    int chunk_idx = 0;

    while (pos < resp_len) {
        size_t remaining = resp_len - pos;
        size_t cLen = remaining < CLAUDE_STREAM_CHUNK_SIZE ? remaining : CLAUDE_STREAM_CHUNK_SIZE;

        if (cLen < CLAUDE_STREAM_CHUNK_SIZE && remaining > 0) {
            cLen = remaining;
        } else {
            while (cLen > 0 && pos + cLen < resp_len &&
                   !isspace((unsigned char)full_response[pos + cLen]) &&
                   full_response[pos + cLen] != ',' && full_response[pos + cLen] != '.' &&
                   full_response[pos + cLen] != '!' && full_response[pos + cLen] != '?' &&
                   full_response[pos + cLen] != ';' && full_response[pos + cLen] != ':' &&
                   full_response[pos + cLen] != '-' && full_response[pos + cLen] != '\n') {
                cLen--;
            }
            if (cLen == 0)
                cLen = 1;
        }

        char chunk_buf[CLAUDE_STREAM_CHUNK_SIZE + 4];
        __builtin_memcpy(chunk_buf, full_response + pos, cLen);
        chunk_buf[cLen] = '\0';
        pos += cLen;

        claude_stream_event_t event;
        AIRY_MEMSET(&event, 0, sizeof(event));
        event.text = chunk_buf;
        event.stop_reason = (pos >= resp_len) ? CLAUDE_STOP_END_TURN : 0;
        event.is_final = (pos >= resp_len);
        handler(&event, user_data);
        chunk_idx++;
    }

    ctx->total_tokens_out += claude_estimate_tokens(full_response);
    return 0;
}
