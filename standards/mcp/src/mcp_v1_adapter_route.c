// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_route.c
 * @brief MCP v1.0 request routing domain (method dispatch and initialize handshake response).
 */

#include "mcp_v1_adapter_internal.h"

#include "mcp_transport.h"
#include "airy_memory.h"
#include "error.h"
#include "types.h"
#include "unified_protocol.h"

#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logging.h"

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
    mcp_log_level_t level = MCP_LOG_INFO;
    if (params_json) {
        char *level_str = strstr(params_json, "\"level\"");
        if (level_str) {
            level_str = strchr(level_str + 7, '"');
            if (level_str) {
                level_str++;
                if (strncmp(level_str, "debug", 5) == 0)
                    level = MCP_LOG_DEBUG;
                else if (strncmp(level_str, "notice", 6) == 0)
                    level = MCP_LOG_NOTICE;
                else if (strncmp(level_str, "warning", 7) == 0)
                    level = MCP_LOG_WARNING;
                else if (strncmp(level_str, "error", 5) == 0)
                    level = MCP_LOG_ERROR;
                else if (strncmp(level_str, "critical", 8) == 0)
                    level = MCP_LOG_CRITICAL;
                else if (strncmp(level_str, "alert", 5) == 0)
                    level = MCP_LOG_ALERT;
                else if (strncmp(level_str, "emergency", 9) == 0)
                    level = MCP_LOG_EMERGENCY;
            }
        }
    }
    return level;
}

static int mcp_v1_build_initialize_response(mcp_v1_context_t *ctx, char **response_json)
{
    size_t len =
        snprintf(NULL, 0,
                 "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":%s,\"resources\":%s,"
                 "\"prompts\":%s,\"sampling\":%s,\"logging\":%s},\"serverInfo\":{\"name\":\"%"
                 "s\",\"version\":\"%s\"}}",
                 MCP_V1_VERSION, (ctx->config.capabilities & MCP_CAP_TOOLS) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_RESOURCES) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_PROMPTS) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_SAMPLING) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_LOGGING) ? "true" : "false",
                 ctx->config.server_name ? ctx->config.server_name : "AgentRT",
                 ctx->config.server_version ? ctx->config.server_version : MCP_V1_VERSION);
    char *resp = AIRY_MALLOC(len + 1);
    if (resp)
        snprintf(resp, len + 1,
                 "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":%s,\"resources\":%s,"
                 "\"prompts\":%s,\"sampling\":%s,\"logging\":%s},\"serverInfo\":{\"name\":\"%"
                 "s\",\"version\":\"%s\"}}",
                 MCP_V1_VERSION, (ctx->config.capabilities & MCP_CAP_TOOLS) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_RESOURCES) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_PROMPTS) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_SAMPLING) ? "true" : "false",
                 (ctx->config.capabilities & MCP_CAP_LOGGING) ? "true" : "false",
                 ctx->config.server_name ? ctx->config.server_name : "AgentRT",
                 ctx->config.server_version ? ctx->config.server_version : MCP_V1_VERSION);
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
    LOG_WARN("method not found in route_request: method=%s, request_counter=%llu", method,
             (unsigned long long)ctx->request_counter);
    airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                     "mcp_v1_adapter: invalid parameter");
    return AIRY_ERR_INVALID_PARAM;
}
