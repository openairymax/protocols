// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file langchain_adapter.c
 * @brief LangChain framework adapter implementation.
 *
 * This file keeps the adapter lifecycle (create/destroy/config defaults/
 * statistics/callback registration), common resource release and the protocol
 * adapter registry (langchain_get_protocol_adapter). Tool registration, chain
 * execution, agent execution, memory management and protocol callbacks live in
 * langchain_adapter_tool.c / langchain_adapter_chain.c / langchain_adapter_agent.c /
 * langchain_adapter_memory.c / langchain_adapter_proto.c.
 */

#define LOG_TAG "langchain_adapter"

#include "langchain_adapter.h"
#include "langchain_adapter_internal.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

langchain_config_t langchain_config_default(void)
{
    langchain_config_t cfg = {0};
    cfg.base_url = "http://127.0.0.1:18789";
    cfg.api_key = NULL;
    cfg.timeout_ms = LANGCHAIN_DEFAULT_TIMEOUT_MS;
    cfg.enable_streaming = true;
    cfg.enable_tracing = false;
    cfg.enable_caching = false;
    cfg.max_concurrent_chains = 16;
    cfg.max_memory_size_kb = 1024;
    cfg.default_llm_model = "gpt-4o";
    cfg.tracing_endpoint = NULL;
    cfg.cache_backend_url = NULL;
    return cfg;
}

langchain_adapter_context_t *langchain_adapter_create(const langchain_config_t *config)
{
    if (!config)
        return NULL;

    langchain_adapter_context_t *ctx =
        (langchain_adapter_context_t *)AIRY_CALLOC(1, sizeof(langchain_adapter_context_t));
    if (!ctx)
        return NULL;

    __builtin_memcpy(&ctx->config, config, sizeof(langchain_config_t));
    if (config->base_url)
        ctx->config.base_url = AIRY_STRDUP(config->base_url);
    if (config->api_key)
        ctx->config.api_key = AIRY_STRDUP(config->api_key);
    if (config->default_llm_model)
        ctx->config.default_llm_model = AIRY_STRDUP(config->default_llm_model);
    if (config->tracing_endpoint)
        ctx->config.tracing_endpoint = AIRY_STRDUP(config->tracing_endpoint);
    if (config->cache_backend_url)
        ctx->config.cache_backend_url = AIRY_STRDUP(config->cache_backend_url);

    ctx->is_initialized = true;
    ctx->tool_count = 0;
    ctx->chain_count = 0;
    ctx->agent_count = 0;
    ctx->memory_count = 0;
    ctx->total_chains_executed = 0;
    ctx->total_tokens_used = 0;
    ctx->total_execution_time_ms = 0.0;

    return ctx;
}

void langchain_adapter_destroy(langchain_adapter_context_t *ctx)
{
    if (!ctx)
        return;

    AIRY_FREE(ctx->config.base_url);
    AIRY_FREE(ctx->config.api_key);
    AIRY_FREE(ctx->config.default_llm_model);
    AIRY_FREE(ctx->config.tracing_endpoint);
    AIRY_FREE(ctx->config.cache_backend_url);

    for (size_t i = 0; i < ctx->tool_count; i++)
        langchain_tool_def_destroy(&ctx->tools[i]);

    for (size_t i = 0; i < ctx->chain_count; i++)
        langchain_chain_instance_destroy(&ctx->chains[i]);

    for (size_t i = 0; i < ctx->agent_count; i++) {
        AIRY_FREE(ctx->agents[i].id);
        AIRY_FREE(ctx->agents[i].name);
        AIRY_FREE(ctx->agents[i].description);
        AIRY_FREE(ctx->agents[i].llm_provider);
    }

    for (size_t i = 0; i < ctx->memory_count; i++)
        langchain_memory_destroy(&ctx->memories[i]);

    AIRY_FREE(ctx->connected_endpoint);
    AIRY_FREE(ctx->send_buffer);

    AIRY_MEMSET(ctx, 0, sizeof(langchain_adapter_context_t));
    AIRY_FREE(ctx);
}

bool langchain_adapter_is_initialized(const langchain_adapter_context_t *ctx)
{
    return ctx && ctx->is_initialized;
}

const char *langchain_adapter_version(void)
{
    return LANGCHAIN_ADAPTER_VERSION;
}

int langchain_set_streaming_handler(langchain_adapter_context_t *ctx,
                                    langchain_streaming_fn handler, void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->streaming_handler = handler;
    ctx->streaming_user_data = user_data;
    return 0;
}

int langchain_set_trace_handler(langchain_adapter_context_t *ctx, langchain_trace_fn handler,
                                void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->trace_handler = handler;
    ctx->trace_user_data = user_data;
    return 0;
}

int langchain_set_llm_callback(langchain_adapter_context_t *ctx, langchain_llm_callback_fn callback,
                               void *user_data)
{
    if (!ctx)
        return AIRY_ERR_NULL_POINTER;
    ctx->llm_callback = callback;
    ctx->llm_callback_data = user_data;
    return 0;
}

int langchain_get_statistics(langchain_adapter_context_t *ctx, char *stats_json, size_t buffer_size)
{
    if (!ctx || !stats_json || buffer_size < 64)
        return AIRY_ERR_NULL_POINTER;

    int written =
        snprintf(stats_json, buffer_size,
                 "{"
                 "\"adapter_version\":\"%s\","
                 "\"total_executions\":%llu,"
                 "\"successful\":%llu,"
                 "\"failure_rate\":%.1f%%,"
                 "\"avg_latency_ms\":%.2f,"
                 "\"registered_tools\":%zu,"
                 "\"active_chains\":%zu,"
                 "\"memories\":%zu"
                 "}",
                 LANGCHAIN_ADAPTER_VERSION, (unsigned long long)ctx->total_chains_executed,
                 (unsigned long long)ctx->total_tokens_used,
                 ctx->total_chains_executed > 0 ?
                     (double)(ctx->total_chains_executed) /
                         (double)(ctx->total_chains_executed + 1) * 100.0 :
                     0.0,
                 ctx->total_chains_executed > 0 ?
                     ctx->total_execution_time_ms / (double)ctx->total_chains_executed :
                     0.0,
                 ctx->tool_count, ctx->chain_count, ctx->memory_count);

    return (written >= 0 && (size_t)written < buffer_size) ? 0 : -2;
}

static int langchain_proto_init(void *context)
{
    if (!context)
        return AIRY_ERR_NULL_POINTER;
    langchain_config_t config = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&config);
    if (!ctx)
        return AIRY_ERR_INVALID_PARAM;
    *(void **)context = ctx;
    return 0;
}

static int langchain_proto_destroy(void *context)
{
    langchain_adapter_destroy((langchain_adapter_context_t *)context);
    return 0;
}

static int langchain_proto_get_version(void *context, char *buf, size_t max_size)
{
    if (!buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    const char *ver = LANGCHAIN_ADAPTER_VERSION;
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, ver, len);
    buf[len] = '\0';
    return 0;
}

static uint32_t langchain_proto_capabilities(void *context)
{
    return (uint32_t)(PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_AGENT_DISCOVERY |
                      PROTO_CAP_RESOURCE_ACCESS);
}

const proto_adapter_t *langchain_get_protocol_adapter(void)
{
    static proto_adapter_t adapter = {0};
    static bool initialized = false;

    if (!initialized) {
        adapter.name = "LangChain";
        adapter.version = LANGCHAIN_ADAPTER_VERSION;
        adapter.description = "LangChain Framework Integration Adapter - LCEL chains, agents, "
                              "tools, memory, RAG support";
        adapter.init = langchain_proto_init;
        adapter.destroy = langchain_proto_destroy;
        adapter.encode = langchain_proto_encode;
        adapter.decode = langchain_proto_decode;
        adapter.connect = langchain_proto_connect;
        adapter.disconnect = langchain_proto_disconnect;
        adapter.is_connected = langchain_proto_is_connected;
        adapter.send = langchain_proto_send;
        adapter.receive = langchain_proto_receive;
        adapter.handle_request = langchain_proto_handle_request;
        adapter.get_version = langchain_proto_get_version;
        adapter.capabilities = langchain_proto_capabilities;
        adapter.get_stats = langchain_proto_get_stats;
        initialized = true;
    }
    return &adapter;
}

void langchain_tool_def_destroy(langchain_tool_def_t *tool)
{
    if (!tool)
        return;
    AIRY_FREE(tool->id);
    AIRY_FREE(tool->name);
    AIRY_FREE(tool->description);
    AIRY_FREE(tool->function_schema_json);
    AIRY_MEMSET(tool, 0, sizeof(langchain_tool_def_t));
}

void langchain_chain_def_destroy(langchain_chain_def_t *chain)
{
    if (!chain)
        return;
    AIRY_FREE(chain->id);
    AIRY_FREE(chain->name);
    for (size_t i = 0; i < chain->step_count; i++)
        AIRY_FREE(chain->step_ids[i]);
    AIRY_FREE(chain->step_ids);
    AIRY_MEMSET(chain, 0, sizeof(langchain_chain_def_t));
}

void langchain_chain_instance_destroy(langchain_chain_instance_t *instance)
{
    if (!instance)
        return;
    AIRY_FREE(instance->id);
    AIRY_FREE(instance->input_schema_json);
    AIRY_FREE(instance->output_schema_json);
    AIRY_FREE(instance->compiled_executable);
    AIRY_MEMSET(instance, 0, sizeof(langchain_chain_instance_t));
}

void langchain_agent_def_destroy(langchain_agent_def_t *agent)
{
    if (!agent)
        return;
    AIRY_FREE(agent->id);
    AIRY_FREE(agent->name);
    AIRY_FREE(agent->llm_id);
    AIRY_FREE(agent->memory_id);
    for (size_t i = 0; i < agent->tool_count; i++)
        AIRY_FREE(agent->tool_ids[i]);
    AIRY_FREE(agent->tool_ids);
    AIRY_MEMSET(agent, 0, sizeof(langchain_agent_def_t));
}

void langchain_memory_destroy(langchain_memory_t *mem)
{
    if (!mem)
        return;
    AIRY_FREE(mem->id);
    AIRY_FREE(mem->summary);
    for (size_t i = 0; i < mem->message_count; i++)
        AIRY_FREE(mem->messages[i]);
    AIRY_FREE(mem->messages);
    AIRY_MEMSET(mem, 0, sizeof(langchain_memory_t));
}

void langchain_execution_result_destroy(langchain_execution_result_t *result)
{
    if (!result)
        return;
    AIRY_FREE(result->chain_id);
    AIRY_FREE(result->input_json);
    AIRY_FREE(result->output_json);
    AIRY_FREE(result->error_message);
    for (size_t i = 0; i < result->intermediate_count; i++)
        AIRY_FREE(result->intermediate_results[i]);
    AIRY_FREE(result->intermediate_results);
    AIRY_MEMSET(result, 0, sizeof(langchain_execution_result_t));
}
