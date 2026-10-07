// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_route.c
 * @brief A2A v0.3 request routing domain (method dispatch and result shaping).
 *
 * Dispatches protocol-neutral method names onto the adapter context and
 * serializes the outcome as cJSON. Method names and result shapes mirror the
 * a2a_d wire contract so a route_request consumer observes exactly the same
 * payloads the daemon used to build in its own service layer.
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <cjson/cJSON.h>
#include <cjson_helpers.h>

#include <string.h>

#include "logging.h"

typedef int (*a2a_route_fn)(a2a_v03_context_t *ctx, const char *params_json, char **response_json);

static void a2a_add_str(cJSON *obj, const char *key, const char *val)
{
    if (val)
        cJSON_AddStringToObject(obj, key, val);
    else
        cJSON_AddNullToObject(obj, key);
}

static cJSON *a2a_card_json(const a2a_agent_card_t *card)
{
    if (!card)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    a2a_add_str(obj, "id", card->id);
    a2a_add_str(obj, "name", card->name);
    a2a_add_str(obj, "description", card->description);
    a2a_add_str(obj, "url", card->url);
    a2a_add_str(obj, "version", card->version);
    cJSON_AddNumberToObject(obj, "protocol_version", (double)card->protocol_version);
    cJSON_AddNumberToObject(obj, "capabilities", (double)(int)card->capabilities);
    cJSON_AddBoolToObject(obj, "available", card->available);

    return obj;
}

static cJSON *a2a_task_json(const a2a_task_t *task)
{
    if (!task)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    a2a_add_str(obj, "id", task->id);
    a2a_add_str(obj, "session_id", task->session_id);
    a2a_add_str(obj, "agent_id", task->agent_id);
    cJSON_AddNumberToObject(obj, "state", (double)task->state);
    a2a_add_str(obj, "description", task->description);
    a2a_add_str(obj, "input_json", task->input_json);
    a2a_add_str(obj, "output_json", task->output_json);
    cJSON_AddNumberToObject(obj, "progress", task->progress);
    cJSON_AddNumberToObject(obj, "created_at", (double)task->created_at);
    cJSON_AddNumberToObject(obj, "updated_at", (double)task->updated_at);
    a2a_add_str(obj, "error_message", task->error_message);

    return obj;
}

static cJSON *a2a_msg_json(const a2a_message_t *msg)
{
    if (!msg)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    a2a_add_str(obj, "role", msg->role);
    cJSON_AddNumberToObject(obj, "type", (double)msg->type);
    a2a_add_str(obj, "content_json", msg->content_json);
    a2a_add_str(obj, "mime_type", msg->mime_type);

    return obj;
}

static int a2a_json_out(cJSON *obj, char **response_json)
{
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!str)
        return AIRY_ERR_OUT_OF_MEMORY;

    *response_json = AIRY_STRDUP(str);
    AIRY_FREE(str);
    return *response_json ? AIRY_SUCCESS : AIRY_ERR_OUT_OF_MEMORY;
}

static void a2a_cards_free(a2a_agent_card_t **results, size_t count)
{
    if (!results)
        return;
    for (size_t i = 0; i < count; i++) {
        if (results[i]) {
            a2a_agent_card_destroy(results[i]);
            AIRY_FREE(results[i]);
        }
    }
    AIRY_FREE(results);
}

static void a2a_msgs_free(a2a_message_t *response, size_t count)
{
    if (!response)
        return;
    for (size_t i = 0; i < count; i++)
        a2a_message_destroy(&response[i]);
}

static int a2a_route_reg(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *id = cJSON_GetObjectItem(root, "id");
        cJSON *name = cJSON_GetObjectItem(root, "name");
        if (!cJSON_IsString(id) || !id->valuestring || id->valuestring[0] == '\0')
            break;
        if (!cJSON_IsString(name))
            break;

        cJSON *url = cJSON_GetObjectItem(root, "url");
        cJSON *protocol_version = cJSON_GetObjectItem(root, "protocol_version");
        cJSON *capabilities = cJSON_GetObjectItem(root, "capabilities");
        cJSON *available = cJSON_GetObjectItem(root, "available");

        a2a_agent_card_t card;
        __builtin_memset(&card, 0, sizeof(card));
        card.id = id->valuestring;
        card.name = name->valuestring;
        card.url = (url && cJSON_IsString(url)) ? url->valuestring : NULL;
        int proto_ver = (protocol_version && cJSON_IsNumber(protocol_version))
                            ? protocol_version->valueint
                            : 3;
        int caps_mask = (capabilities && cJSON_IsNumber(capabilities)) ? capabilities->valueint : 0;
        card.protocol_version = proto_ver;
        card.capabilities = (a2a_capability_t)caps_mask;
        card.available = available ? cJSON_IsTrue(available) : true;

        rc = a2a_v03_register_agent(ctx, &card);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddBoolToObject(result, "registered", true);
        cJSON_AddStringToObject(result, "agent_id", id->valuestring);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_unreg(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *agent_id = cJSON_GetObjectItem(root, "agent_id");
        if (!cJSON_IsString(agent_id))
            break;

        rc = a2a_v03_unregister_agent(ctx, agent_id->valuestring);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddBoolToObject(result, "unregistered", true);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_disc(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *capability = cJSON_GetObjectItem(root, "capability");
        cJSON *skill = cJSON_GetObjectItem(root, "skill");
        const char *cap_str = (capability && cJSON_IsString(capability)) ? capability->valuestring
                                                                         : NULL;
        const char *skill_str = (skill && cJSON_IsString(skill)) ? skill->valuestring : NULL;

        a2a_agent_card_t **results = NULL;
        size_t count = 0;
        rc = a2a_v03_discover_agents(ctx, cap_str, skill_str, &results, &count);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *arr = cJSON_CreateArray();
        if (!arr) {
            a2a_cards_free(results, count);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        for (size_t i = 0; i < count; i++) {
            cJSON *item = a2a_card_json(results[i]);
            if (item)
                cJSON_AddItemToArray(arr, item);
        }
        a2a_cards_free(results, count);

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            cJSON_Delete(arr);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddItemToObject(result, "agents", arr);
        cJSON_AddNumberToObject(result, "count", (double)count);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_new(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *agent_id = cJSON_GetObjectItem(root, "agent_id");
        cJSON *description = cJSON_GetObjectItem(root, "description");
        if (!cJSON_IsString(agent_id))
            break;
        if (!cJSON_IsString(description))
            break;

        cJSON *input = cJSON_GetObjectItem(root, "input");
        const char *input_str = (input && cJSON_IsString(input)) ? input->valuestring : NULL;

        a2a_task_t *task = NULL;
        rc = a2a_v03_create_task(ctx, agent_id->valuestring, description->valuestring, input_str,
                                 &task);
        if (rc != AIRY_SUCCESS || !task)
            break;

        cJSON *obj = a2a_task_json(task);
        if (!obj) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON *result = cJSON_CreateObject();
        if (!result) {
            cJSON_Delete(obj);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddItemToObject(result, "task", obj);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_upd(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *task_id = cJSON_GetObjectItem(root, "task_id");
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (!cJSON_IsString(task_id))
            break;
        if (!cJSON_IsNumber(state))
            break;

        cJSON *output = cJSON_GetObjectItem(root, "output");
        cJSON *progress = cJSON_GetObjectItem(root, "progress");
        const char *output_str = (output && cJSON_IsString(output)) ? output->valuestring : NULL;
        double prog = (progress && cJSON_IsNumber(progress)) ? progress->valuedouble : 0.0;

        rc = a2a_v03_update_task(ctx, task_id->valuestring, (a2a_task_state_t)state->valueint,
                                 output_str, prog);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddBoolToObject(result, "updated", true);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_cncl(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *task_id = cJSON_GetObjectItem(root, "task_id");
        if (!cJSON_IsString(task_id))
            break;

        cJSON *reason = cJSON_GetObjectItem(root, "reason");
        const char *reason_str = (reason && cJSON_IsString(reason)) ? reason->valuestring : NULL;

        rc = a2a_v03_cancel_task(ctx, task_id->valuestring, reason_str);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddBoolToObject(result, "canceled", true);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_get(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *task_id = cJSON_GetObjectItem(root, "task_id");
        if (!cJSON_IsString(task_id))
            break;

        a2a_task_t *task = NULL;
        rc = a2a_v03_get_task(ctx, task_id->valuestring, &task);
        if (rc != AIRY_SUCCESS || !task)
            break;

        cJSON *obj = a2a_task_json(task);
        if (!obj) {
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON *result = cJSON_CreateObject();
        if (!result) {
            cJSON_Delete(obj);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddItemToObject(result, "task", obj);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_send(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    int rc = AIRY_ERR_INVALID_PARAM;
    do {
        CJSON_PARSE_GUARD(root, params_json && params_json[0] ? params_json : "{}", { break; });
        cJSON *target = cJSON_GetObjectItem(root, "target_agent_id");
        cJSON *role = cJSON_GetObjectItem(root, "role");
        cJSON *content = cJSON_GetObjectItem(root, "content");
        if (!cJSON_IsString(target))
            break;
        if (!cJSON_IsString(role))
            break;
        if (!cJSON_IsString(content))
            break;

        a2a_message_t message;
        __builtin_memset(&message, 0, sizeof(message));
        message.role = role->valuestring;
        message.type = A2A_MSG_TEXT;
        message.content_json = content->valuestring;

        a2a_message_t *response = NULL;
        size_t response_count = 0;
        rc = a2a_v03_send_message(ctx, target->valuestring, &message, &response, &response_count);
        if (rc != AIRY_SUCCESS)
            break;

        cJSON *arr = cJSON_CreateArray();
        if (!arr) {
            a2a_msgs_free(response, response_count);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        for (size_t i = 0; i < response_count; i++) {
            cJSON *item = a2a_msg_json(&response[i]);
            if (item)
                cJSON_AddItemToArray(arr, item);
        }
        a2a_msgs_free(response, response_count);

        cJSON *result = cJSON_CreateObject();
        if (!result) {
            cJSON_Delete(arr);
            rc = AIRY_ERR_OUT_OF_MEMORY;
            break;
        }
        cJSON_AddItemToObject(result, "responses", arr);
        cJSON_AddNumberToObject(result, "count", (double)response_count);
        rc = a2a_json_out(result, response_json);
    } while (0);
    return rc;
}

static int a2a_route_stats(a2a_v03_context_t *ctx, char **response_json, int with_caps)
{
    cJSON *result = cJSON_CreateObject();
    if (!result)
        return AIRY_ERR_OUT_OF_MEMORY;

    cJSON_AddNumberToObject(result, "agent_count", (double)a2a_v03_get_agent_count(ctx));
    cJSON_AddNumberToObject(result, "task_count", (double)a2a_v03_get_task_count(ctx));
    if (with_caps)
        cJSON_AddNumberToObject(result, "capabilities", (double)a2a_v03_get_capabilities(ctx));

    return a2a_json_out(result, response_json);
}

static int a2a_route_cnt(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    (void)params_json;
    return a2a_route_stats(ctx, response_json, 0);
}

static int a2a_route_sta(a2a_v03_context_t *ctx, const char *params_json, char **response_json)
{
    (void)params_json;
    return a2a_route_stats(ctx, response_json, 1);
}

static const struct {
    const char *name;
    a2a_route_fn fn;
} a2a_routes[] = {
    { "register_agent", a2a_route_reg },
    { "unregister_agent", a2a_route_unreg },
    { "discover_agents", a2a_route_disc },
    { "create_task", a2a_route_new },
    { "update_task", a2a_route_upd },
    { "cancel_task", a2a_route_cncl },
    { "get_task", a2a_route_get },
    { "send_message", a2a_route_send },
    { "count", a2a_route_cnt },
    { "stats", a2a_route_sta },
};

int a2a_v03_route_request(a2a_v03_context_t *ctx, const char *method, const char *params_json,
                          char **response_json)
{
    if (!ctx || !method || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_route_request: failed");
        return AIRY_ERR_UNKNOWN;
    }

    *response_json = NULL;

    for (size_t i = 0; i < sizeof(a2a_routes) / sizeof(a2a_routes[0]); i++) {
        if (strcmp(method, a2a_routes[i].name) == 0) {
            int rc = a2a_routes[i].fn(ctx, params_json, response_json);
            if (rc != AIRY_SUCCESS) {
                AIRY_FREE(*response_json);
                *response_json = NULL;
            }
            return rc;
        }
    }

    *response_json = AIRY_STRDUP("{\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    AIRY_LOG_WARN("method not found in route_request: method=%s", method);
    airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                     "a2a_v03_route_request: invalid parameter");
    return AIRY_ERR_INVALID_PARAM;
}
