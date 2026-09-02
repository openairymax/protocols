// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_chat.c
 * @brief OpenAI enterprise adapter chat completion domain (sync/streaming completion, request building).
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
#include <time.h>

#include "platform.h"

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#endif

int oai_chat_completion(openai_handle_t handle, const openai_chat_request_t *request,
                           openai_chat_response_t *out_response)
{
    if (!handle || !request || !out_response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "oai_chat_completion: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    uint32_t est_tokens = 100;
    if (request->num_messages > 0 && request->messages) {
        for (size_t i = 0; i < request->num_messages && i < 256; i++) {
            if (request->messages[i].content)
                est_tokens += (uint32_t)(strlen(request->messages[i].content) / 4);
        }
    }

    openai_rate_result_t rate_status = oai_check_rate_limit(adapter, est_tokens);
    if (rate_status != OPENAI_RATE_OK) {
        AIRY_MEMSET(out_response, 0, sizeof(*out_response));
        out_response->created = (uint64_t)time(NULL);
        AIRY_STRNCPY_TERM(out_response->model, request->model ? request->model : "gpt-4o",
                          sizeof(out_response->model));
        out_response->finish_reasons = AIRY_CALLOC(1, sizeof(openai_finish_reason_t));
        if (out_response->finish_reasons)
            out_response->finish_reasons[0] = OPENAI_FINISH_RATE_LIMITED;
        openai_on_429(adapter);
        airy_err_push_ex(AIRY_EAGAIN, __FILE__, __LINE__, __func__,
                         "openai: rate limited (429)");
        return AIRY_EAGAIN;
    }

    AIRY_MEMSET(out_response, 0, sizeof(*out_response));
    AIRY_STRNCPY_TERM(out_response->model, request->model ? request->model : "gpt-4o",
                      sizeof(out_response->model));
    out_response->created = (uint64_t)time(NULL);

    __attribute__((unused)) uint64_t ts_start_ms = airy_time_ms();

#ifndef AIRY_HAS_CURL
    return -ENOSYS;
#else
    if (!adapter->config.api_key || !adapter->config.api_key[0]) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
        return AIRY_ERR_UNKNOWN;
    }

    cJSON *req_json = cJSON_CreateObject();
    cJSON_AddStringToObject(req_json, "model", request->model ? request->model : "gpt-4o");
    cJSON_AddNumberToObject(req_json, "max_tokens",
                            request->max_tokens > 0 ? request->max_tokens : 4096);
    cJSON_AddNumberToObject(req_json, "temperature",
                            request->temperature >= 0 ? request->temperature : 0.7);
    if (request->top_p > 0 && request->top_p <= 1.0) {
        cJSON_AddNumberToObject(req_json, "top_p", request->top_p);
    }

    if (request->tools && request->tool_count > 0) {
        cJSON *tools_arr = cJSON_CreateArray();
        for (size_t i = 0; i < request->tool_count; i++) {
            cJSON *tool_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_obj, "type", "function");
            cJSON *func_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(func_obj, "name",
                                    request->tools[i].function.name ?
                                        request->tools[i].function.name :
                                        "");
            cJSON_AddStringToObject(func_obj, "description",
                                    request->tools[i].function.description ?
                                        request->tools[i].function.description :
                                        "");
            if (request->tools[i].function.parameters_schema_json) {

                CJSON_PARSE_GUARD(params, request->tools[i].function.parameters_schema_json,
                                  { (void)0; });
                if (params) {
                    cJSON_AddItemToObject(func_obj, "parameters", params);
                    params = NULL;
                } else {
                    cJSON_AddItemToObject(func_obj, "parameters", cJSON_CreateObject());
                }
            } else {
                cJSON_AddItemToObject(func_obj, "parameters", cJSON_CreateObject());
            }
            cJSON_AddItemToObject(tool_obj, "function", func_obj);
            cJSON_AddItemToArray(tools_arr, tool_obj);
        }
        cJSON_AddItemToObject(req_json, "tools", tools_arr);
    }

    cJSON *msgs_arr = cJSON_CreateArray();
    for (size_t i = 0; i < request->num_messages && i < 256; i++) {
        cJSON *msg_obj = cJSON_CreateObject();
        const char *role_str = "user";
        switch (request->messages[i].role) {
        case OPENAI_ROLE_SYSTEM:
            role_str = "system";
            break;
        case OPENAI_ROLE_ASSISTANT:
            role_str = "assistant";
            break;
        case OPENAI_ROLE_TOOL:
            role_str = "tool";
            break;
        case OPENAI_ROLE_FUNCTION:
            role_str = "function";
            break;
        default:
            break;
        }
        cJSON_AddStringToObject(msg_obj, "role", role_str);
        if (request->messages[i].content)
            cJSON_AddStringToObject(msg_obj, "content", request->messages[i].content);
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }
    cJSON_AddItemToObject(req_json, "messages", msgs_arr);

    char *req_str = cJSON_PrintUnformatted(req_json);
    cJSON_Delete(req_json);

    char api_response[8192];
    AIRY_MEMSET(api_response, 0, sizeof(api_response));
    int api_result =
        openai_api_call(adapter->config.api_key, adapter->config.base_url, "/chat/completions",
                        req_str, api_response, sizeof(api_response));
    AIRY_FREE(req_str);
    req_str = NULL;

    uint64_t ts_end_ms = airy_time_ms();
    double latency_ms = (double)(ts_end_ms - ts_start_ms);

    if (api_result > 0) {
        char content_buf[OPENAI_MAX_RESPONSE_LEN];
        AIRY_MEMSET(content_buf, 0, sizeof(content_buf));
        openai_usage_t api_usage = {0};
        int parse_result =
            oai_parse_chat_resp(api_response, content_buf, sizeof(content_buf), &api_usage);
        if (parse_result == 0 && content_buf[0] != '\0') {
            out_response->choices = AIRY_CALLOC(1, sizeof(openai_message_t));
            if (out_response->choices) {
                out_response->choices[0].content = AIRY_STRDUP(content_buf);
                out_response->choices[0].role = OPENAI_ROLE_ASSISTANT;
            }
            out_response->choice_count = 1;
            out_response->finish_reasons = AIRY_CALLOC(1, sizeof(openai_finish_reason_t));
            if (out_response->finish_reasons)
                out_response->finish_reasons[0] = OPENAI_FINISH_STOP;
            out_response->usage = api_usage;
        } else {
            airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                             "openai: unknown error");
            return AIRY_ERR_UNKNOWN;
        }
    } else {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
        return AIRY_ERR_UNKNOWN;
    }

    adapter->stats_chat_completions++;
    adapter->stats_total_input_tokens += out_response->usage.prompt_tokens;
    adapter->stats_total_output_tokens += out_response->usage.completion_tokens;
    oai_record_latency(adapter, latency_ms);
    openai_record_request(adapter, out_response->usage.prompt_tokens,
                          out_response->usage.completion_tokens);
#endif

    return 0;
}

int oai_stream_chat(openai_handle_t handle, const openai_chat_request_t *request,
                                     openai_streaming_handler_t on_chunk, void *user_data,
                                     openai_chat_response_t *final_summary)
{
    if (!handle || !request || !on_chunk || !final_summary) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "oai_stream_chat: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

#ifndef AIRY_HAS_CURL
    (void)request;
    (void)on_chunk;
    (void)user_data;
    return -ENOSYS;
#else
    if (!adapter->config.api_key || !adapter->config.api_key[0]) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
        return AIRY_ERR_UNKNOWN;
    }

    uint64_t ts_start_ms = airy_time_ms();

    AIRY_MEMSET(final_summary, 0, sizeof(*final_summary));
    final_summary->created = (uint64_t)time(NULL);
    AIRY_STRNCPY_TERM(final_summary->model, request->model ? request->model : "gpt-4o",
                      sizeof(final_summary->model));

    cJSON *req_json = cJSON_CreateObject();
    cJSON_AddStringToObject(req_json, "model", request->model ? request->model : "gpt-4o");
    cJSON_AddNumberToObject(req_json, "max_tokens",
                            request->max_tokens > 0 ? request->max_tokens : 4096);
    cJSON_AddBoolToObject(req_json, "stream", 1);

    cJSON *msgs_arr = cJSON_CreateArray();
    for (size_t i = 0; i < request->num_messages && i < 256; i++) {
        cJSON *msg_obj = cJSON_CreateObject();
        const char *role_str = "user";
        switch (request->messages[i].role) {
        case OPENAI_ROLE_SYSTEM:
            role_str = "system";
            break;
        case OPENAI_ROLE_ASSISTANT:
            role_str = "assistant";
            break;
        case OPENAI_ROLE_TOOL:
            role_str = "tool";
            break;
        case OPENAI_ROLE_FUNCTION:
            role_str = "function";
            break;
        default:
            break;
        }
        cJSON_AddStringToObject(msg_obj, "role", role_str);
        if (request->messages[i].content)
            cJSON_AddStringToObject(msg_obj, "content", request->messages[i].content);
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }
    cJSON_AddItemToObject(req_json, "messages", msgs_arr);

    char *req_str = cJSON_PrintUnformatted(req_json);
    cJSON_Delete(req_json);

    char api_response[16384];
    AIRY_MEMSET(api_response, 0, sizeof(api_response));
    int api_result =
        openai_api_call(adapter->config.api_key, adapter->config.base_url, "/chat/completions",
                        req_str, api_response, sizeof(api_response));
    AIRY_FREE(req_str);
    req_str = NULL;

    if (api_result <= 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
        return AIRY_ERR_UNKNOWN;
    }

    char full_response[OPENAI_MAX_RESPONSE_LEN];
    AIRY_MEMSET(full_response, 0, sizeof(full_response));
    openai_usage_t api_usage = {0};
    int parse_result =
        oai_parse_chat_resp(api_response, full_response, sizeof(full_response), &api_usage);
    if (parse_result != 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
        return AIRY_ERR_UNKNOWN;
    }

    size_t response_len = strlen(full_response);
    size_t pos = 0;

    while (pos < response_len) {
        size_t remaining = response_len - pos;
        size_t chunk_len =
            remaining < OPENAI_STREAM_CHUNK_SIZE ? remaining : OPENAI_STREAM_CHUNK_SIZE;

        while (chunk_len > 0 && pos + chunk_len < response_len &&
               !isspace((unsigned char)full_response[pos + chunk_len]) &&
               full_response[pos + chunk_len] != ',' && full_response[pos + chunk_len] != '.' &&
               full_response[pos + chunk_len] != '!' && full_response[pos + chunk_len] != '?' &&
               full_response[pos + chunk_len] != ';' && full_response[pos + chunk_len] != ':' &&
               full_response[pos + chunk_len] != '-' && full_response[pos + chunk_len] != '\n') {
            chunk_len--;
        }
        if (chunk_len == 0)
            chunk_len = 1;

        char chunk_buf[OPENAI_STREAM_CHUNK_SIZE + 4];
        __builtin_memcpy(chunk_buf, full_response + pos, chunk_len);
        chunk_buf[chunk_len] = '\0';
        pos += chunk_len;

        on_chunk(chunk_buf, request->model,
                 (pos >= response_len) ? OPENAI_FINISH_STOP : OPENAI_FINISH_LENGTH, user_data);
    }

    uint64_t ts_end_ms = airy_time_ms();
    double latency_ms = (double)(ts_end_ms - ts_start_ms);

    final_summary->choices = AIRY_CALLOC(1, sizeof(openai_message_t));
    if (final_summary->choices) {
        final_summary->choices[0].role = OPENAI_ROLE_ASSISTANT;
        final_summary->choices[0].content = AIRY_STRDUP(full_response);
        final_summary->choice_count = 1;
    }
    final_summary->finish_reasons = AIRY_CALLOC(1, sizeof(openai_finish_reason_t));
    if (final_summary->finish_reasons)
        final_summary->finish_reasons[0] = OPENAI_FINISH_STOP;
    final_summary->usage = api_usage;

    adapter->stats_streaming_sessions++;
    adapter->stats_total_input_tokens += final_summary->usage.prompt_tokens;
    adapter->stats_total_output_tokens += final_summary->usage.completion_tokens;
    oai_record_latency(adapter, latency_ms);
#endif

    return 0;
}
