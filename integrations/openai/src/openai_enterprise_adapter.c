// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openai_enterprise_adapter.c
 * @brief OpenAI API enterprise adapter implementation.
 *
 * Implements an OpenAI API compatible adapter, supporting:
 * - /v1/chat/completions - chat completion (sync + streaming)
 * - /v1/embeddings - vector embeddings
 * - /v1/models - model list
 * - streaming SSE response handling
 *
 * @since 0.1.0
 */

#include "openai_enterprise_adapter.h"
#include "openai_enterprise_adapter_internal.h"

#include "platform.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef AIRY_HAS_CURL
#include <curl/curl.h>
#endif

#ifdef AIRY_HAS_CJSON
#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#endif

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/include/error.h"
#include "error.h"

struct openai_enterprise_adapter_s *g_openai_instance = NULL;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

int openai_create(openai_enterprise_config_t config, openai_handle_t *out_handle)
{
    if (!out_handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai_create: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct openai_enterprise_adapter_s *adapter =
        AIRY_CALLOC(1, sizeof(struct openai_enterprise_adapter_s));
    if (!adapter) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openai: out of memory");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    adapter->config = config;
    adapter->model_count = 0;
    adapter->request_counter = 1;
    adapter->initialized = true;
    adapter->stats_chat_completions = 0;
    adapter->stats_embeddings = 0;
    adapter->stats_streaming_sessions = 0;
    adapter->stats_total_input_tokens = 0;
    adapter->stats_total_output_tokens = 0;
    adapter->stats_total_latency_ms = 0.0;
    adapter->stats_min_latency_ms = 999999.0;
    adapter->stats_max_latency_ms = 0.0;
    adapter->stats_latency_index = 0;
    adapter->stats_latency_count = 0;
    adapter->rate_limit_rpm = OPENAI_RATE_LIMIT_RPM_DEFAULT;
    adapter->rate_limit_tpm = OPENAI_RATE_LIMIT_TPM_DEFAULT;
    adapter->rate_window_start = time(NULL);
    adapter->rate_window_requests = 0;
    adapter->rate_window_tokens = 0;
    adapter->rate_429_count = 0;
    adapter->rate_last_429_time = 0;
    adapter->rate_backoff_multiplier = 1.0;
    adapter->rate_backoff_until = 0;

    openai_register_builtin_models(adapter);

    g_openai_instance = adapter;
    *out_handle = (openai_handle_t)adapter;
    return 0;
}

void openai_destroy(openai_handle_t handle)
{
    if (!handle)
        return;
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;

    for (size_t i = 0; i < adapter->model_count; i++) {
        AIRY_FREE((void *)adapter->models[i].id);
        AIRY_FREE((void *)adapter->models[i].name);
        AIRY_FREE((void *)adapter->models[i].owned_by);
    }

    AIRY_FREE(adapter->last_response_body);

    adapter->initialized = false;
    if (g_openai_instance == adapter)
        g_openai_instance = NULL;
    AIRY_FREE(adapter);
}

bool openai_is_initialized(openai_handle_t handle)
{
    if (!handle)
        return false;
    return ((struct openai_enterprise_adapter_s *)handle)->initialized;
}

const char *openai_version(void)
{
    return "AgentRT-OpenAI/" OPENAI_VERSION;
}

const protocol_adapter_t *openai_enterprise_get_adapter(void)
{
    static protocol_adapter_t s_adapter;
    static bool s_init = false;
    if (!s_init) {
        AIRY_MEMSET(&s_adapter, 0, sizeof(s_adapter));
        s_adapter.type = AIRY_PROTOCOL_OPENAI;
        s_adapter.name = "openai-enterprise";
        s_adapter.version = OPENAI_ADAPTER_VERSION;
        s_adapter.description = "OpenAI Enterprise API Adapter";
        s_adapter.context = NULL;
        s_adapter.init = openai_adapter_init_cb;
        s_adapter.destroy = openai_adapter_destroy_cb;
        s_adapter.encode = openai_adapter_encode_cb;
        s_adapter.decode = openai_adapter_decode_cb;
        s_adapter.connect = openai_adapter_connect_cb;
        s_adapter.disconnect = openai_adapter_disconnect_cb;
        s_adapter.is_connected = openai_adapter_is_connected_cb;
        s_adapter.send = openai_adapter_send_cb;
        s_adapter.receive = openai_adapter_receive_cb;
        s_adapter.handle_request = openai_adapter_handle_request_cb;
        s_adapter.get_version = openai_adapter_get_version_cb;
        s_adapter.capabilities = openai_adapter_capabilities_cb;
        s_adapter.get_stats = openai_adapter_get_stats_cb;
        s_init = true;
    }
    return &s_adapter;
}

void openai_chat_response_destroy(openai_chat_response_t *resp)
{
    if (!resp)
        return;
    AIRY_FREE(resp->id);
    AIRY_FREE(resp->object);
    AIRY_FREE(resp->model);
    if (resp->choices) {
        for (size_t i = 0; i < resp->choice_count; i++) {
            AIRY_FREE(resp->choices[i].content);
            AIRY_FREE(resp->choices[i].name);
            AIRY_FREE(resp->choices[i].tool_call_id);
            AIRY_FREE(resp->choices[i].function_name);
            AIRY_FREE(resp->choices[i].function_arguments_json);
        }
        AIRY_FREE(resp->choices);
    }
    AIRY_FREE(resp->finish_reasons);
    if (resp->tool_calls) {
        for (size_t i = 0; i < resp->tool_call_count; i++) {
            AIRY_FREE(resp->tool_calls[i].id);
            AIRY_FREE(resp->tool_calls[i].type);
            AIRY_FREE(resp->tool_calls[i].function_name);
            AIRY_FREE(resp->tool_calls[i].function_arguments_json);
        }
        AIRY_FREE(resp->tool_calls);
    }
    AIRY_MEMSET(resp, 0, sizeof(*resp));
}

void openai_embedding_response_destroy(openai_embedding_response_t *resp)
{
    if (!resp)
        return;
    AIRY_FREE(resp->id);
    AIRY_FREE(resp->object);
    AIRY_FREE(resp->model);
    AIRY_FREE(resp->embeddings);
    AIRY_MEMSET(resp, 0, sizeof(*resp));
}

void openai_message_destroy(openai_message_t *msg)
{
    if (!msg)
        return;
    AIRY_FREE(msg->content);
    AIRY_FREE(msg->name);
    AIRY_FREE(msg->tool_call_id);
    AIRY_FREE(msg->function_name);
    AIRY_FREE(msg->function_arguments_json);
    AIRY_MEMSET(msg, 0, sizeof(*msg));
}

void openai_model_destroy(openai_model_t *model)
{
    if (!model)
        return;
    AIRY_FREE(model->id);
    AIRY_FREE(model->name);
    AIRY_FREE(model->owned_by);
    AIRY_MEMSET(model, 0, sizeof(*model));
}
