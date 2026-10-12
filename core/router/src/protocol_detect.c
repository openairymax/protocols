// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_detect.c
 * @brief Payload-level protocol detection implementation.
 *
 * One structured (cJSON) signature probe for JSON-RPC / MCP / A2A / OpenAI.
 * MCP and A2A requests ride inside JSON-RPC envelopes with namespaced
 * method names, so the envelope is classified by method prefix rather than
 * by scanning the whole body for substrings.
 */

#include "../include/protocol_detect.h"

#include "airy_protocol_interface.h"

#include <cjson/cJSON.h>

#include <string.h>

#define DETECT_UNKNOWN AIRY_PROTOCOL_COUNT

static int has_key(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}

static int method_has_prefix(const cJSON *method, const char *prefix)
{
    if (!cJSON_IsString(method) || !method->valuestring)
        return 0;
    size_t n = strlen(prefix);
    return strncmp(method->valuestring, prefix, n) == 0;
}

protocol_type_t proto_detect_body(const char *data, size_t size)
{
    if (!data || size == 0)
        return DETECT_UNKNOWN;

    cJSON *json = cJSON_ParseWithLength(data, size);
    if (!json)
        return DETECT_UNKNOWN;

    protocol_type_t type = DETECT_UNKNOWN;
    const cJSON *jsonrpc = cJSON_GetObjectItemCaseSensitive(json, "jsonrpc");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(json, "method");

    if (cJSON_IsString(jsonrpc) && strcmp(jsonrpc->valuestring, "2.0") == 0 &&
        cJSON_IsString(method)) {
        if (method_has_prefix(method, "tools/") || method_has_prefix(method, "resources/") ||
            method_has_prefix(method, "prompts/"))
            type = AIRY_PROTOCOL_MCP;
        else if (method_has_prefix(method, "tasks/") || method_has_prefix(method, "task/") ||
                 method_has_prefix(method, "message/"))
            type = AIRY_PROTOCOL_A2A;
        else
            type = AIRY_PROTOCOL_JSON_RPC;
    } else if (has_key(json, "model") &&
               (has_key(json, "messages") || has_key(json, "prompt"))) {
        type = proto_interface_parse_type("openai");
    } else if (has_key(json, "agent_id") &&
               (has_key(json, "task_id") || has_key(json, "message"))) {
        type = AIRY_PROTOCOL_A2A;
    } else if (has_key(json, "agentCard") || has_key(json, "task") || has_key(json, "a2a")) {
        type = AIRY_PROTOCOL_A2A;
    }

    cJSON_Delete(json);
    return type;
}
