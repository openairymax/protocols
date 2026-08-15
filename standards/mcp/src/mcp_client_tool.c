// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client_tool.c
 * @brief MCP client tool-call domain (tools/list, tools/call, text extraction).
 *
 * Single responsibility: fetch the external tool list, call external tools and
 * return the full JSON-RPC response, and extract the first text content from
 * the response (for the gateway to assemble MCP responses).
 */

#define LOG_TAG "mcp_client"

#include "mcp_client.h"
#include "mcp_client_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"

#include <cjson/cJSON.h>
#include <cjson_helpers.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifndef _WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/**
  * @brief Escape a JSON string and quote it (same as json_string_escape in mcp_v1_adapter.c)
  * @return Quoted JSON string (AIRY_MALLOC)
 */
static char *json_string_escape(const char *str)
{
    if (!str)
        return AIRY_STRDUP("null");
    size_t len = strlen(str);
    size_t escaped_len = len * 2 + 3;
    char *escaped = AIRY_MALLOC(escaped_len);
    if (!escaped)
        return NULL;
    size_t j = 0;
    escaped[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
        case '"':
            escaped[j++] = '\\';
            escaped[j++] = '"';
            break;
        case '\\':
            escaped[j++] = '\\';
            escaped[j++] = '\\';
            break;
        case '\n':
            escaped[j++] = '\\';
            escaped[j++] = 'n';
            break;
        case '\r':
            escaped[j++] = '\\';
            escaped[j++] = 'r';
            break;
        case '\t':
            escaped[j++] = '\\';
            escaped[j++] = 't';
            break;
        default:
            escaped[j++] = str[i];
            break;
        }
    }
    escaped[j++] = '"';
    escaped[j] = '\0';
    return escaped;
}

int mcp_client_list_tools(mcp_client_t *c, mcp_client_tool_list_t *out)
{
    if (!c || !out)
        return AIRY_ERR_INVALID_PARAM;
    if (c->transport == MCP_CLIENT_TRANSPORT_NONE)
        return MCP_CLIENT_ERR_NOT_CONNECTED;
    AIRY_MEMSET(out, 0, sizeof(*out));

    int rc = client_ensure_initialized(c);
    if (rc != 0)
        return rc;

    char *resp = NULL;
    rc = client_rpc_exchange(c, "tools/list", "{}", true, &resp);
    if (rc != 0)
        return rc;

    CJSON_PARSE_GUARD(root, resp, {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    });
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        AIRY_LOG_WARN("mcp client '%s': tools/list failed: %s", c->name,
                 msg && cJSON_IsString(msg) && msg->valuestring ? msg->valuestring :
                                                                  "unknown error");
        AIRY_FREE(resp);
        return MCP_CLIENT_ERR_RPC_ERROR;
    }
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    }
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (!cJSON_IsArray(tools)) {
        AIRY_FREE(resp);
        return AIRY_ERR_PARSE_ERROR;
    }

    int n = cJSON_GetArraySize(tools);
    if (n <= 0) {
        AIRY_FREE(resp);
        return 0;
    }
    mcp_client_tool_t *arr = AIRY_CALLOC((size_t)n, sizeof(mcp_client_tool_t));
    if (!arr) {
        AIRY_FREE(resp);
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    size_t filled = 0;
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_GetArrayItem(tools, i);
        cJSON *jname = cJSON_GetObjectItem(t, "name");
        if (!cJSON_IsString(jname) || !jname->valuestring)
            continue;
        arr[filled].name = AIRY_STRDUP(jname->valuestring);
        if (!arr[filled].name)
            continue;
        cJSON *jdesc = cJSON_GetObjectItem(t, "description");
        if (cJSON_IsString(jdesc) && jdesc->valuestring)
            arr[filled].description = AIRY_STRDUP(jdesc->valuestring);
        cJSON *jschema = cJSON_GetObjectItem(t, "inputSchema");
        if (cJSON_IsObject(jschema))
            arr[filled].input_schema_json = cJSON_PrintUnformatted(jschema);
        filled++;
    }
    AIRY_FREE(resp);

    if (filled == 0) {
        mcp_client_tool_list_t tmp;
        tmp.tools = arr;
        tmp.count = 0;
        mcp_client_tool_list_free(&tmp);
        return 0;
    }
    out->tools = arr;
    out->count = filled;
    return 0;
}

int mcp_client_call_tool(mcp_client_t *c, const char *name, const char *arguments_json,
                         char **result_json)
{
    if (!c || !name || !result_json)
        return AIRY_ERR_INVALID_PARAM;
    if (c->transport == MCP_CLIENT_TRANSPORT_NONE)
        return MCP_CLIENT_ERR_NOT_CONNECTED;
    *result_json = NULL;

    int rc = client_ensure_initialized(c);
    if (rc != 0)
        return rc;

    cJSON *params = cJSON_CreateObject();
    if (!params)
        return AIRY_ERR_OUT_OF_MEMORY;
    cJSON_AddStringToObject(params, "name", name);
    cJSON *args = cJSON_Parse(arguments_json && arguments_json[0] ? arguments_json : "{}");
    cJSON_AddItemToObject(params, "arguments", args ? args : cJSON_CreateObject());
    char *params_str = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (!params_str)
        return AIRY_ERR_OUT_OF_MEMORY;

    rc = client_rpc_exchange(c, "tools/call", params_str, true, result_json);
    AIRY_FREE(params_str);
    return rc;
}

int mcp_client_extract_text(const char *response_json, char **text_json)
{
    *text_json = NULL;
    if (!response_json)
        return AIRY_ERR_INVALID_PARAM;
    CJSON_PARSE_GUARD(root, response_json, { return AIRY_ERR_PARSE_ERROR; });

    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        const char *raw =
            (cJSON_IsString(msg) && msg->valuestring) ? msg->valuestring : "MCP RPC error";
        *text_json = json_string_escape(raw);
        return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result))
        return AIRY_ERR_PARSE_ERROR;
    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        cJSON *text = cJSON_GetObjectItem(first, "text");
        if (cJSON_IsString(text) && text->valuestring) {
            *text_json = json_string_escape(text->valuestring);
            return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
        }

        *text_json = cJSON_PrintUnformatted(first);
        return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
    }

    *text_json = cJSON_PrintUnformatted(result);
    return *text_json ? 0 : AIRY_ERR_OUT_OF_MEMORY;
}
