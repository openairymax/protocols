// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file mcp_client_rpc.c
 * @brief MCP client protocol handshake and JSON-RPC exchange domain.
 *
 * Single responsibility: JSON-RPC 2.0 request building, id-matched synchronous
 * exchange, initialize handshake (idempotent) and the initialized notification.
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

static int64_t now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

/**
  * @brief Check whether a frame is the response for the given id
  * @return 1 match; 0 no match (notification/other id); <0 parse error
 */
static int response_id_matches(const char *frame, uint64_t expected)
{
    CJSON_PARSE_GUARD(root, frame, { return AIRY_ERR_PARSE_ERROR; });
    cJSON *rid = cJSON_GetObjectItem(root, "id");
    if (!rid)
        return 0;
    if (cJSON_IsNumber(rid))
        return ((uint64_t)rid->valuedouble == expected) ? 1 : 0;
    if (cJSON_IsString(rid) && rid->valuestring)
        return (strtoull(rid->valuestring, NULL, 10) == expected) ? 1 : 0;
    return 0;
}

/**
  * @brief Send a JSON-RPC request and, optionally, wait for the matching-id response
  * @param When expect_response is true, out_response holds the response text (AIRY_MALLOC)
 */
int client_rpc_exchange(mcp_client_t *c, const char *method, const char *params_json,
                        bool expect_response, char **out_response)
{
    uint64_t id = ++c->request_id;
    cJSON *req = cJSON_CreateObject();
    if (!req)
        return AIRY_ERR_OUT_OF_MEMORY;
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", (double)id);
    cJSON_AddStringToObject(req, "method", method);
    cJSON *params = cJSON_Parse(params_json && params_json[0] ? params_json : "{}");
    cJSON_AddItemToObject(req, "params", params ? params : cJSON_CreateObject());
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_str)
        return AIRY_ERR_OUT_OF_MEMORY;

    int rc;
    if (c->transport == MCP_CLIENT_TRANSPORT_STDIO) {
        rc = stdio_send_message(c, req_str);
        AIRY_FREE(req_str);
        if (rc != 0)
            return rc;
        if (!expect_response)
            return 0;
        int64_t deadline = now_ms() + MCP_CLIENT_DEFAULT_TIMEOUT_MS;
        for (;;) {
            int64_t remain = deadline - now_ms();
            if (remain <= 0) {
                LOG_WARN("mcp client '%s': rpc %s timeout", c->name, method);
                return AIRY_ERR_TIMEOUT;
            }
            char *frame = NULL;
            rc = stdio_read_message(c, &frame, (int)remain);
            if (rc != 0)
                return rc;
            int m = response_id_matches(frame, id);
            if (m == 1) {
                *out_response = frame;
                return 0;
            }
            if (m < 0) {
                AIRY_FREE(frame);
                return m;
            }
            AIRY_FREE(frame);
        }
    } else if (c->transport == MCP_CLIENT_TRANSPORT_HTTP) {
        char *resp_body = NULL;
        rc = http_send_message(c, req_str, &resp_body);
        AIRY_FREE(req_str);
        if (rc != 0)
            return rc;
        if (!expect_response) {
            AIRY_FREE(resp_body);
            return 0;
        }
        if (response_id_matches(resp_body, id) < 0) {
            AIRY_FREE(resp_body);
            return AIRY_ERR_PARSE_ERROR;
        }
        *out_response = resp_body;
        return 0;
    }
    AIRY_FREE(req_str);
    return MCP_CLIENT_ERR_NOT_CONNECTED;
}

static int parse_initialize_response(mcp_client_t *c, const char *resp)
{
    CJSON_PARSE_GUARD(root, resp, { return AIRY_ERR_PARSE_ERROR; });
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        LOG_WARN("mcp client '%s': initialize rejected: %s", c->name,
                 msg && cJSON_IsString(msg) && msg->valuestring ? msg->valuestring :
                                                                  "unknown error");
        return MCP_CLIENT_ERR_RPC_ERROR;
    }
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result))
        return AIRY_ERR_PARSE_ERROR;
    cJSON *si = cJSON_GetObjectItem(result, "serverInfo");
    if (cJSON_IsObject(si)) {
        cJSON *sn = cJSON_GetObjectItem(si, "name");
        if (cJSON_IsString(sn) && sn->valuestring) {
            c->server_name = AIRY_STRDUP(sn->valuestring);
            LOG_INFO("mcp client '%s': initialized, remote server=%s", c->name, sn->valuestring);
        }
    } else {
        LOG_INFO("mcp client '%s': initialized", c->name);
    }
    return 0;
}

/**
  * @brief Perform the initialize handshake on first use (idempotent)
 */
int client_ensure_initialized(mcp_client_t *c)
{
    if (c->initialized)
        return 0;
    const char *params = "{\"protocolVersion\":\"2024-11-05\","
                         "\"capabilities\":{},"
                         "\"clientInfo\":{\"name\":\"agentrt-gateway\",\"version\":\"0.1.1\"}}";
    char *resp = NULL;
    int rc = client_rpc_exchange(c, "initialize", params, true, &resp);
    if (rc != 0)
        return rc;
    rc = parse_initialize_response(c, resp);
    AIRY_FREE(resp);
    if (rc != 0)
        return rc;
    c->initialized = true;

    rc = client_rpc_exchange(c, "notifications/initialized", "{}", false, NULL);
    if (rc != 0)
        LOG_WARN("mcp client '%s': failed to send initialized notification (rc=%d)", c->name, rc);
    return 0;
}
