// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_route.c
 * @brief MCP v1.0 request routing domain (method dispatch and initialize handshake response).
 */

#include "mcp_v1_adapter_internal.h"

static void mcp_v1_extract_string_param(const char *params_json, const char *key, char *buf,
                                        size_t buf_size)
{
    if (!params_json)
        return;
    do {
        CJSON_PARSE_GUARD(pj, params_json, { break; });
        cJSON *item = cJSON_GetObjectItem(pj, key);
        if (cJSON_IsString(item) && item->valuestring) {
            AIRY_STRNCPY_TERM(buf, item->valuestring, buf_size);
        }
    } while (0);
}

static mcp_log_level_t mcp_v1_parse_log_level(const char *params_json)
{
    static const struct {
        const char *name;
        mcp_log_level_t level;
    } table[] = {
        { "debug", MCP_LOG_DEBUG },       { "notice", MCP_LOG_NOTICE },
        { "warning", MCP_LOG_WARNING },   { "error", MCP_LOG_ERROR },
        { "critical", MCP_LOG_CRITICAL }, { "alert", MCP_LOG_ALERT },
        { "emergency", MCP_LOG_EMERGENCY },
    };
    mcp_log_level_t level = MCP_LOG_INFO;

    if (!params_json)
        return level;
    do {
        CJSON_PARSE_GUARD(pj, params_json, { break; });
        cJSON *item = cJSON_GetObjectItem(pj, "level");
        if (!cJSON_IsString(item) || !item->valuestring)
            break;
        for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
            if (strcmp(item->valuestring, table[i].name) == 0) {
                level = table[i].level;
                break;
            }
        }
    } while (0);
    return level;
}

/**
 * @brief initialize 握手响应渲染：buf 为 NULL 时测长（snprintf 语义）。
 */
static int init_resp_fill(char *buf, size_t buf_len, const mcp_v1_config_t *cfg)
{
    return snprintf(buf, buf_len,
                    "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":%s,\"resources\":%s,"
                    "\"prompts\":%s,\"sampling\":%s,\"logging\":%s},\"serverInfo\":{\"name\":\"%"
                    "s\",\"version\":\"%s\"}}",
                    MCP_V1_VERSION, (cfg->capabilities & MCP_CAP_TOOLS) ? "true" : "false",
                    (cfg->capabilities & MCP_CAP_RESOURCES) ? "true" : "false",
                    (cfg->capabilities & MCP_CAP_PROMPTS) ? "true" : "false",
                    (cfg->capabilities & MCP_CAP_SAMPLING) ? "true" : "false",
                    (cfg->capabilities & MCP_CAP_LOGGING) ? "true" : "false",
                    cfg->server_name ? cfg->server_name : "AgentRT",
                    cfg->server_version ? cfg->server_version : MCP_V1_VERSION);
}

static int mcp_v1_build_initialize_response(mcp_v1_context_t *ctx, char **response_json)
{
    size_t len = (size_t)init_resp_fill(NULL, 0, &ctx->config);
    char *resp = AIRY_MALLOC(len + 1);
    if (resp)
        init_resp_fill(resp, len + 1, &ctx->config);
    *response_json = resp;
    return 0;
}

int mcp_v1_route_request(mcp_v1_context_t *ctx, const char *method, const char *params_json,
                         char **response_json)
{
    if (!ctx || !method || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_route_request: failed");
        return AIRY_ERR_UNKNOWN;
    }

    ctx->request_counter++;

    if (strcmp(method, "tools/list") == 0) {
        return mcp_v1_handle_tools_list(ctx, response_json);
    } else if (strcmp(method, "tools/call") == 0) {
        char tool_name[256] = {0};
        mcp_v1_extract_string_param(params_json, "name", tool_name, sizeof(tool_name));
        return mcp_v1_handle_tools_call(ctx, tool_name[0] ? tool_name : "unknown", params_json,
                                        response_json);
    } else if (strcmp(method, "resources/list") == 0) {
        return mcp_v1_handle_resources_list(ctx, response_json);
    } else if (strcmp(method, "resources/read") == 0) {
        char resource_uri[512] = {0};
        mcp_v1_extract_string_param(params_json, "uri", resource_uri, sizeof(resource_uri));
        return mcp_v1_handle_resources_read(ctx, resource_uri[0] ? resource_uri : "unknown",
                                            response_json);
    } else if (strcmp(method, "resources/templates/list") == 0) {
        return mcp_v1_handle_resources_templates(ctx, response_json);
    } else if (strcmp(method, "prompts/list") == 0) {
        return mcp_v1_handle_prompts_list(ctx, response_json);
    } else if (strcmp(method, "prompts/get") == 0) {
        char prompt_name[256] = {0};
        mcp_v1_extract_string_param(params_json, "name", prompt_name, sizeof(prompt_name));
        return mcp_v1_handle_prompts_get(ctx, prompt_name[0] ? prompt_name : "unknown", params_json,
                                         response_json);
    } else if (strcmp(method, "logging/setLogLevel") == 0) {
        return mcp_v1_set_log_level(ctx, mcp_v1_parse_log_level(params_json));
    } else if (strcmp(method, "initialize") == 0) {
        return mcp_v1_build_initialize_response(ctx, response_json);
    }

    *response_json = AIRY_STRDUP("{\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    AIRY_LOG_WARN("method not found in route_request: method=%s, request_counter=%llu", method,
             (unsigned long long)ctx->request_counter);
    airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                     "mcp_v1_adapter: invalid parameter");
    return AIRY_ERR_INVALID_PARAM;
}
