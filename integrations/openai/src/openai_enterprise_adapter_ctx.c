// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_ctx.c
 * @brief OpenAI enterprise adapter context and new-style API domain (openai_enterprise_* series and request routing).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/include/error.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

openai_enterprise_config_t openai_enterprise_config_default(void)
{
    openai_enterprise_config_t cfg;
    AIRY_MEMSET(&cfg, 0, sizeof(cfg));
    cfg.api_key = NULL;
    /* P0-09 fix: use string literals to avoid dynamic allocation (as in mcp_v1_config_default).
     *
      * Historical issue: the old code AIRY_STRDUP'd base_url/default_model, but config is
      * returned by value and callers (e.g. test_openai_adapter.c) rarely free them; ASAN
     * 26B("https:
     *
      * reported leaks. Fix: the default config points at literals (read-only, static storage).
      * openai_create() and openai_enterprise_context_create() copy config by value;
      * they share the literal pointers; openai_destroy() never frees config strings, so safe. */
    cfg.base_url = (char *)"https://api.openai.com/v1";
    cfg.default_model = (char *)"gpt-4o";
    cfg.organization = NULL;
    cfg.max_retries = 3;
    cfg.retry_base_ms = 1000;
    cfg.request_timeout_ms = 60000;
    cfg.enable_streaming = true;
    cfg.enable_function_calling = true;
    cfg.enable_rate_limiting = true;
    cfg.enable_audit_logging = false;
    cfg.rpm_limit = 60;
    cfg.tpm_limit = 150000;
    cfg.max_tokens_default = 4096;
    cfg.temperature_default = 0.7;
    cfg.top_p_default = 1.0;
    cfg.strict_schema_validation = false;
    return cfg;
}

openai_enterprise_context_t *openai_enterprise_context_create(
    const openai_enterprise_config_t *config)
{
    if (!config)
        return NULL;
    openai_enterprise_context_t *ctx = AIRY_CALLOC(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->config = *config;
    openai_handle_t handle = NULL;
    openai_enterprise_config_t mutable_cfg = *config;
    int rc = openai_create(mutable_cfg, &handle);
    if (rc != 0) {
        AIRY_FREE(ctx);
        return NULL;
    }
    ctx->handle = handle;
    return ctx;
}

void openai_enterprise_context_destroy(openai_enterprise_context_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->handle)
        openai_destroy(ctx->handle);
    AIRY_FREE(ctx);
}

int openai_enterprise_register_model(openai_enterprise_context_t *ctx, const openai_model_t *model)
{
    if (!ctx || !ctx->handle || !model) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_register_model: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)ctx->handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }
    if (adapter->model_count >= OPENAI_MAX_MODELS) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "openai: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }
    openai_model_t *slot = &adapter->models[adapter->model_count];
    slot->id = model->id ? AIRY_STRDUP(model->id) : NULL;
    slot->name = model->name ? AIRY_STRDUP(model->name) : NULL;
    slot->owned_by = model->owned_by ? AIRY_STRDUP(model->owned_by) : NULL;
    slot->capabilities = model->capabilities;
    slot->max_context_tokens = model->max_context_tokens;
    slot->max_output_tokens = model->max_output_tokens;
    slot->cost_per_1k_input = model->cost_per_1k_input;
    slot->cost_per_1k_output = model->cost_per_1k_output;
    slot->is_default = model->is_default;
    slot->is_available = model->is_available;
    adapter->model_count++;
    return 0;
}

int openai_enterprise_chat_completion(openai_enterprise_context_t *ctx, const char *model,
                                      const openai_message_t *messages, size_t message_count,
                                      const openai_tool_def_t *tools, size_t tool_count,
                                      double temperature, double top_p, int max_tokens,
                                      openai_chat_response_t *response)
{
    if (!ctx || !ctx->handle || !response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }
    const char *effective_model = model ? model : "gpt-4o";
    openai_chat_request_t req;
    AIRY_MEMSET(&req, 0, sizeof(req));
    req.model = AIRY_STRDUP(effective_model);
    if (messages && message_count > 0) {
        req.messages = (openai_message_t *)messages;
        req.num_messages = message_count;
    }
    req.temperature = (float)(temperature > 0 ? temperature : 1.0);
    req.top_p = (float)(top_p > 0 && top_p <= 1.0 ? top_p : 1.0);
    req.max_tokens = max_tokens > 0 ? max_tokens : 4096;
    req.tools = tools;
    req.tool_count = tool_count;
    return openai_chat_completion(ctx->handle, &req, response);
}

int openai_enterprise_chat_streaming(openai_enterprise_context_t *ctx, const char *model,
                                     const openai_message_t *messages, size_t message_count,
                                     openai_streaming_handler_t handler, void *user_data)
{
    if (!ctx || !ctx->handle || !handler) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_chat_streaming: failed");
        return AIRY_ERR_UNKNOWN;
    }
    const char *effective_model = model ? model : "gpt-4o";
    openai_chat_request_t req;
    AIRY_MEMSET(&req, 0, sizeof(req));
    req.model = AIRY_STRDUP(effective_model);
    if (messages && message_count > 0) {
        req.messages = (openai_message_t *)messages;
        req.num_messages = message_count;
    }
    openai_chat_response_t final_resp;
    AIRY_MEMSET(&final_resp, 0, sizeof(final_resp));
    return openai_chat_completion_streaming(ctx->handle, &req, handler, user_data, &final_resp);
}

int openai_enterprise_embeddings(openai_enterprise_context_t *ctx, const char *model,
                                 const char **inputs, size_t input_count,
                                 openai_embedding_response_t *response)
{
    if (!ctx || !ctx->handle || !response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_embeddings: failed");
        return AIRY_ERR_UNKNOWN;
    }
    openai_embedding_request_t req;
    AIRY_MEMSET(&req, 0, sizeof(req));
    req.input_text =
        (inputs && input_count > 0 && inputs[0]) ? AIRY_STRDUP(inputs[0]) : AIRY_STRDUP("");
    req.embedding_dim = 1536;
    req.model = model ? AIRY_STRDUP(model) : AIRY_STRDUP("text-embedding-3-small");
    int result = openai_create_embedding(ctx->handle, &req, response);
    AIRY_FREE(req.input_text);
    req.input_text = NULL;
    AIRY_FREE(req.model);
    req.model = NULL;
    return result;
}

int openai_enterprise_list_models(openai_enterprise_context_t *ctx, openai_model_t **models,
                                  size_t *model_count)
{
    if (!ctx || !ctx->handle || !models || !model_count) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_list_models: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)ctx->handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }
    *model_count = adapter->model_count;
    if (adapter->model_count == 0) {
        *models = NULL;
        return 0;
    }
    *models = AIRY_CALLOC(adapter->model_count, sizeof(openai_model_t));
    if (!*models) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openai: out of memory");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < adapter->model_count; i++) {
        (*models)[i].id = adapter->models[i].id ? AIRY_STRDUP(adapter->models[i].id) : NULL;
        (*models)[i].name = adapter->models[i].name ? AIRY_STRDUP(adapter->models[i].name) : NULL;
        (*models)[i].owned_by =
            adapter->models[i].owned_by ? AIRY_STRDUP(adapter->models[i].owned_by) : NULL;
        (*models)[i].capabilities = adapter->models[i].capabilities;
        (*models)[i].max_context_tokens = adapter->models[i].max_context_tokens;
        (*models)[i].max_output_tokens = adapter->models[i].max_output_tokens;
        (*models)[i].cost_per_1k_input = adapter->models[i].cost_per_1k_input;
        (*models)[i].cost_per_1k_output = adapter->models[i].cost_per_1k_output;
        (*models)[i].is_default = adapter->models[i].is_default;
        (*models)[i].is_available = adapter->models[i].is_available;
    }
    return 0;
}

bool openai_enterprise_check_rate_limit(openai_enterprise_context_t *ctx, int estimated_tokens)
{
    if (!ctx || !ctx->handle)
        return false;
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)ctx->handle;
    if (!adapter->initialized)
        return false;
    openai_rate_result_t result = openai_check_rate_limit(adapter, (uint32_t)estimated_tokens);
    return result == OPENAI_RATE_OK;
}

int openai_enterprise_set_chat_handler(openai_enterprise_context_t *ctx,
                                       openai_chat_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_set_chat_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->chat_handler = handler;
    ctx->chat_handler_user_data = user_data;
    return 0;
}

int openai_enterprise_set_embedding_handler(openai_enterprise_context_t *ctx,
                                            openai_embedding_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_set_embedding_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->embedding_handler = handler;
    ctx->embedding_handler_user_data = user_data;
    return 0;
}

int openai_enterprise_set_audit_handler(openai_enterprise_context_t *ctx,
                                        openai_audit_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_set_audit_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    ctx->audit_handler = handler;
    ctx->audit_handler_user_data = user_data;
    return 0;
}

int openai_enterprise_route_request(openai_enterprise_context_t *ctx, const char *path,
                                    const char *method, const char *body_json, char **response_json)
{
    if (!ctx || !path || !method || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_enterprise_route_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    *response_json = NULL;
    if (strcmp(path, "/v1/chat/completions") == 0) {
        openai_message_t msg = {0};
        msg.role = OPENAI_ROLE_USER;
        msg.content = (body_json && body_json[0]) ? AIRY_STRDUP(body_json) : AIRY_STRDUP("hello");
        openai_chat_response_t resp;
        AIRY_MEMSET(&resp, 0, sizeof(resp));
        int rc =
            openai_enterprise_chat_completion(ctx, NULL, &msg, 1, NULL, 0, 0.7, 1.0, 4096, &resp);
        if (rc == 0 && resp.choices && resp.choice_count > 0) {
            const char *content = resp.choices[0].content ? resp.choices[0].content : "";
            size_t content_len = strlen(content);
            size_t escaped_cap = content_len * 2 + 128;
            char *escaped_content = (char *)AIRY_MALLOC(escaped_cap);
            if (escaped_content) {
                json_escape_string(content, escaped_content, escaped_cap);
                size_t sz = 64 + strlen(escaped_content);
                char *json = (char *)AIRY_MALLOC(sz);
                if (json)
                    snprintf(json, sz, "{\"result\":\"%s\"}", escaped_content);
                *response_json = json;
                AIRY_FREE(escaped_content);
                escaped_content = NULL;
            } else {
                size_t sz = 64;
                char *json = (char *)AIRY_MALLOC(sz);
                if (json)
                    snprintf(json, sz, "{\"result\":\"\"}");
                *response_json = json;
            }
        } else {
            size_t err_sz = 256;
            char *err_json = (char *)AIRY_MALLOC(err_sz);
            if (err_json)
                snprintf(err_json, err_sz,
                         "{\"error\":{\"message\":\"chat completion "
                         "failed\",\"type\":\"api_error\",\"code\":null}}");
            *response_json = err_json;
        }
        openai_free_chat_response(&resp);
        AIRY_FREE(msg.content);
        return rc;
    }
    if (strcmp(path, "/v1/embeddings") == 0) {
        const char *inputs[1] = {body_json ? body_json : ""};
        openai_embedding_response_t emb_resp;
        AIRY_MEMSET(&emb_resp, 0, sizeof(emb_resp));
        int rc = openai_enterprise_embeddings(ctx, "text-embedding-ada-002", inputs, 1, &emb_resp);
        if (rc != 0) {
            *response_json = NULL;
            openai_embedding_response_destroy(&emb_resp);
            return rc;
        }
        size_t json_sz = 512 + (emb_resp.embedding_dim > 0 ? emb_resp.embedding_dim * 16 : 0);
        const char *model_name = emb_resp.model ? emb_resp.model : "text-embedding-ada-002";
        char escaped_model[256];
        json_escape_string(model_name, escaped_model, sizeof(escaped_model));
        json_sz += strlen(escaped_model);
        char *json = (char *)AIRY_MALLOC(json_sz);
        if (!json) {
            openai_embedding_response_destroy(&emb_resp);
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "json_escape_string: allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        size_t pos = 0;
        pos += snprintf(json + pos, json_sz - pos,
                        "{\"object\":\"list\",\"model\":\"%s\",\"data\":[{\"object\":\"embedding\","
                        "\"index\":0,\"embedding\":[",
                        escaped_model);
        if (emb_resp.embeddings && emb_resp.embedding_dim > 0) {
            size_t show_dim = emb_resp.embedding_dim < 8 ? emb_resp.embedding_dim : 8;
            for (size_t j = 0; j < show_dim && pos < json_sz - 64; j++) {
                if (j > 0)
                    pos += snprintf(json + pos, json_sz - pos, ",");
                pos += snprintf(json + pos, json_sz - pos, "%.6f", emb_resp.embeddings[j]);
            }
            if (emb_resp.embedding_dim > 8)
                pos += snprintf(json + pos, json_sz - pos, ",...(%zu_more)",
                                emb_resp.embedding_dim - 8);
        }
        pos += snprintf(json + pos, json_sz - pos,
                        "]}],\"usage\":{\"prompt_tokens\":%zu,\"total_tokens\":%zu}}",
                        (size_t)emb_resp.usage.prompt_tokens, (size_t)emb_resp.usage.total_tokens);
        *response_json = json;
        openai_embedding_response_destroy(&emb_resp);
        return 0;
    }
    if (strcmp(path, "/v1/models") == 0) {
        openai_model_t *models = NULL;
        size_t count = 0;
        int rc = openai_enterprise_list_models(ctx, &models, &count);
        if (rc != 0) {
            *response_json = NULL;
            return rc;
        }
        size_t json_sz = 256 + count * 256;
        char *json = (char *)AIRY_MALLOC(json_sz);
        if (!json) {
            for (size_t i = 0; i < count; i++) {
                AIRY_FREE(models[i].id);
                AIRY_FREE(models[i].name);
                AIRY_FREE(models[i].owned_by);
            }
            AIRY_FREE(models);
            return AIRY_EINVAL;
        }
        size_t pos = 0;
        pos += snprintf(json + pos, json_sz - pos, "{\"object\":\"list\",\"data\":[");
        for (size_t i = 0; i < count && pos < json_sz - 128; i++) {
            if (i > 0)
                pos += snprintf(json + pos, json_sz - pos, ",");
            char escaped_id[256];
            char escaped_owned_by[256];
            const char *model_id = models[i].id ? models[i].id : "";
            const char *owned_by = models[i].owned_by ? models[i].owned_by : "";
            json_escape_string(model_id, escaped_id, sizeof(escaped_id));
            json_escape_string(owned_by, escaped_owned_by, sizeof(escaped_owned_by));
            pos += snprintf(json + pos, json_sz - pos,
                            "{\"id\":\"%s\",\"object\":\"model\",\"owned_by\":\"%s\"}", escaped_id,
                            escaped_owned_by);
            AIRY_FREE(models[i].id);
            models[i].id = NULL;
            AIRY_FREE(models[i].name);
            models[i].name = NULL;
            AIRY_FREE(models[i].owned_by);
            models[i].owned_by = NULL;
        }
        AIRY_FREE(models);
        models = NULL;
        pos += snprintf(json + pos, json_sz - pos, "]}");
        *response_json = json;
        return 0;
    }
    *response_json = NULL;
    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "openai: unknown error");
    return AIRY_ERR_UNKNOWN;
}
