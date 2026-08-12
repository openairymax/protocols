// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file claude_adapter.c
 * @brief Anthropic Claude API adapter implementation.
 *
 * Production implementation using the real Claude API via HTTPS.
 * Requires AIRY_HAS_CURL to be defined for compilation.
 *
 * This file keeps the adapter lifecycle (create/destroy/config defaults/
 * statistics) and the protocol adapter registry (claude_get_protocol_adapter).
 * API request building, model catalog, protocol callbacks and resource
 * release live in claude_adapter_http.c / claude_adapter_model.c /
 * claude_adapter_proto.c / claude_adapter_cleanup.c.
 *
 * BAN-19 compliance: fails closed without curl; never uses mocks or
 * template-generated fake responses.
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
#endif

void *g_claude_proto_context = NULL;

claude_config_t claude_config_default(void)
{
    claude_config_t cfg = {0};
    cfg.api_key = NULL;
    cfg.base_url = "https://api.anthropic.com";
    cfg.default_model = CLAUDE_MODEL_CLAUDE_3_5_SONNET;
    cfg.max_tokens = CLAUDE_MAX_OUTPUT_TOKENS;
    cfg.temperature = 1.0;
    cfg.top_p = -1.0;
    cfg.top_k = -1.0;
    cfg.enable_streaming = true;
    cfg.enable_tool_use = true;
    cfg.enable_extended_thinking = false;
    cfg.thinking_mode = CLAUDE_THINKING_DISABLED;
    cfg.thinking_budget_tokens = 10000;
    cfg.cache_control = CLAUDE_CACHE_EPHEMERAL;
    cfg.timeout_ms = CLAUDE_DEFAULT_TIMEOUT_MS;
    cfg.max_retries = CLAUDE_MAX_RETRIES;
    cfg.enable_safety_filtering = true;
    cfg.system_prompt = NULL;
    cfg.metadata_json = NULL;
    return cfg;
}

claude_adapter_context_t *claude_adapter_create(const claude_config_t *config)
{
    if (!config)
        return NULL;

    claude_adapter_context_t *ctx =
        (claude_adapter_context_t *)AIRY_CALLOC(1, sizeof(claude_adapter_context_t));
    if (!ctx)
        return NULL;

    __builtin_memcpy(&ctx->config, config, sizeof(claude_config_t));

    if (config->api_key)
        ctx->config.api_key = AIRY_STRDUP(config->api_key);
    if (config->base_url)
        ctx->config.base_url = AIRY_STRDUP(config->base_url);
    if (config->system_prompt)
        ctx->config.system_prompt = AIRY_STRDUP(config->system_prompt);
    if (config->metadata_json)
        ctx->config.metadata_json = AIRY_STRDUP(config->metadata_json);

    ctx->initialized = true;
    ctx->total_requests = 0;
    ctx->total_tokens_in = 0;
    ctx->total_tokens_out = 0;
    ctx->total_tool_calls = 0;

    return ctx;
}

void claude_adapter_destroy(claude_adapter_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->config.api_key) {
        size_t key_len = strlen(ctx->config.api_key);
        AIRY_MEMSET(ctx->config.api_key, 0, key_len);
        AIRY_FREE(ctx->config.api_key);
    }
    AIRY_FREE(ctx->config.base_url);
    AIRY_FREE(ctx->config.system_prompt);
    AIRY_FREE(ctx->config.metadata_json);

    AIRY_FREE(ctx->connected_endpoint);
    AIRY_FREE(ctx->send_buffer);

    AIRY_MEMSET(ctx, 0, sizeof(claude_adapter_context_t));
    AIRY_FREE(ctx);
}

bool claude_adapter_is_initialized(const claude_adapter_context_t *ctx)
{
    return ctx && ctx->initialized;
}

const char *claude_adapter_version(void)
{
    return CLAUDE_ADAPTER_VERSION;
}

int claude_set_message_handler(claude_adapter_context_t *ctx, claude_message_handler_t handler,
                               void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->message_handler = handler;
    ctx->message_handler_data = user_data;
    return 0;
}

int claude_set_stream_handler(claude_adapter_context_t *ctx, claude_stream_handler_t handler,
                              void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->stream_handler = handler;
    ctx->stream_handler_data = user_data;
    return 0;
}

int claude_set_tool_use_handler(claude_adapter_context_t *ctx, claude_tool_use_handler_t handler,
                                void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->tool_use_handler = handler;
    ctx->tool_use_handler_data = user_data;
    return 0;
}

int claude_get_usage_statistics(claude_adapter_context_t *ctx, char *stats_json, size_t buffer_size)
{
    if (!ctx || !stats_json || buffer_size < 64)
        return AIRY_ERR_NULL_POINTER;

    int written =
        snprintf(stats_json, buffer_size,
                 "{"
                 "\"adapter_version\":\"%s\","
                 "\"api_version\":\"%s\","
                 "\"default_model\":\"%s\","
                 "\"total_requests\":%llu,"
                 "\"total_input_tokens\":%llu,"
                 "\"total_output_tokens\":%llu,"
                 "\"total_tool_calls\":%llu,"
                 "\"available_models\":%d"
                 "}",
                 CLAUDE_ADAPTER_VERSION, CLAUDE_API_VERSION,
                 claude_model_id_to_api_name(ctx->config.default_model),
                 (unsigned long long)ctx->total_requests, (unsigned long long)ctx->total_tokens_in,
                 (unsigned long long)ctx->total_tokens_out,
                 (unsigned long long)ctx->total_tool_calls, g_builtin_model_count);

    return (written >= 0 && (size_t)written < buffer_size) ? 0 : -2;
}

static int claude_proto_init(void *context)
{
    if (!context)
        return AIRY_ERR_NULL_POINTER;

    claude_config_t cfg = claude_config_default();
    claude_adapter_context_t *ctx = claude_adapter_create(&cfg);
    if (!ctx)
        return AIRY_ERR_INVALID_PARAM;

    g_claude_proto_context = ctx;
    *(void **)context = ctx;
    return 0;
}

static int claude_proto_destroy(void *context)
{
    if (context) {
        claude_adapter_destroy((claude_adapter_context_t *)context);
        if (g_claude_proto_context == context)
            g_claude_proto_context = NULL;
    }
    return 0;
}

static int claude_proto_handle_request(void *context, const void *req, void **resp)
{
    if (!context || !req || !resp)
        return AIRY_ERR_NULL_POINTER;

    claude_adapter_context_t *ctx = (claude_adapter_context_t *)context;
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    const unified_message_t *request = (const unified_message_t *)req;

    const char *user_content = "";
    const char *system_content = "";

#ifdef AIRY_HAS_CURL
    if (request->payload) {

        do {
            CJSON_PARSE_GUARD(json, request->payload, { break; });
            cJSON *msgs = cJSON_GetObjectItem(json, "messages");
            if (cJSON_IsArray(msgs)) {
                int mcount = cJSON_GetArraySize(msgs);
                for (int i = mcount - 1; i >= 0; i--) {
                    cJSON *mi = cJSON_GetArrayItem(msgs, i);
                    cJSON *role = cJSON_GetObjectItem(mi, "role");
                    cJSON *content = cJSON_GetObjectItem(mi, "content");
                    const char *rs = role ? cJSON_GetStringValue(role) : NULL;
                    const char *cs = content ? cJSON_GetStringValue(content) : NULL;
                    if (rs && strcmp(rs, "user") == 0 && cs)
                        user_content = cs;
                    else if (rs && strcmp(rs, "system") == 0 && cs)
                        system_content = cs;
                }
            }

        } while (0);
    }
#else
    if (request->payload) {
        user_content = request->payload;
    }
#endif

    if (request->body && request->body_length > 0)
        user_content = (const char *)request->body;

    char resp_text[CLAUDE_MAX_RESPONSE_LEN];
    AIRY_MEMSET(resp_text, 0, sizeof(resp_text));
    claude_generate_response(user_content, system_content, resp_text, sizeof(resp_text));

    unified_message_t *response = (unified_message_t *)AIRY_CALLOC(1, sizeof(unified_message_t));
    if (!response)
        return AIRY_ERR_OUT_OF_MEMORY;

    size_t resp_len = strlen(resp_text);
    response->payload = AIRY_STRDUP(resp_text);
    response->payload_size = resp_len;
    response->status = 200;
    if (request) {
        AIRY_STRNCPY_TERM(response->correlation_id, request->correlation_id,
                          sizeof(response->correlation_id));
    }

    ctx->total_requests++;
    ctx->total_tokens_in +=
        claude_estimate_tokens(user_content) + claude_estimate_tokens(system_content);
    ctx->total_tokens_out += claude_estimate_tokens(resp_text);

    *resp = response;
    return 0;
}

static int claude_proto_get_version(void *context, char *buf, size_t max_size)
{
    (void)context;
    if (!buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    const char *ver = claude_adapter_version();
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, ver, len);
    buf[len] = '\0';
    return 0;
}

static uint32_t claude_proto_capabilities(void *context)
{
    (void)context;
    return (uint32_t)(PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_VISION |
                      PROTO_CAP_EXTENDED_THINKING);
}

const proto_adapter_t *claude_get_protocol_adapter(void)
{
    static proto_adapter_t adapter = {0};
    static bool initialized = false;

    if (!initialized) {
        adapter.name = "Claude";
        adapter.version = CLAUDE_ADAPTER_VERSION;
        adapter.description = "Anthropic Claude API Adapter - advanced LLM with extended thinking, "
                              "vision, and tool use capabilities";
        adapter.type = AIRY_PROTOCOL_CLAUDE;
        adapter.init = claude_proto_init;
        adapter.destroy = claude_proto_destroy;
        adapter.encode = claude_proto_encode;
        adapter.decode = claude_proto_decode;
        adapter.connect = claude_proto_connect;
        adapter.disconnect = claude_proto_disconnect;
        adapter.is_connected = claude_proto_is_connected;
        adapter.send = claude_proto_send;
        adapter.receive = claude_proto_receive;
        adapter.handle_request = claude_proto_handle_request;
        adapter.get_version = claude_proto_get_version;
        adapter.capabilities = claude_proto_capabilities;
        adapter.get_stats = claude_proto_get_stats;
        initialized = true;
    }

    return &adapter;
}
