// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_cb.c
 * @brief MCP v1.0 protocol adapter callback domain (protocol adapter interface implementation and registry entry).
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

static int mcp_adapter_init(void *context)
{
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_init: failed");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_config_t config = mcp_v1_config_default();
    mcp_v1_context_t *new_ctx = mcp_v1_context_create(&config);
    if (!new_ctx) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    __builtin_memcpy(ctx, new_ctx, sizeof(mcp_v1_context_t));
    AIRY_FREE(new_ctx);
    return 0;
}

static int mcp_adapter_destroy(void *context)
{
    if (!context)
        return 0;
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    /* Free internal heap fields (as mcp_v1_context_destroy does) but never the shell:
      * the adapter's context may be the static global s_mcp_default_context (P0-01 default
      * context obtained by gateway_d via mcp_v1_get_adapter()); freeing the shell
      * triggers an ASan bad-free (free on a non-malloc address). The holder owns the shell
      * (protocol_adapter_t.context); destroy only frees internal resources. */
    for (size_t i = 0; i < ctx->tool_count; i++) {
        AIRY_FREE(ctx->tools[i].tool.name);
        AIRY_FREE(ctx->tools[i].tool.description);
        AIRY_FREE(ctx->tools[i].tool.input_schema_json);
    }
    AIRY_FREE(ctx->tools);

    for (size_t i = 0; i < ctx->resource_count; i++) {
        AIRY_FREE(ctx->resources[i].resource.uri);
        AIRY_FREE(ctx->resources[i].resource.name);
        AIRY_FREE(ctx->resources[i].resource.description);
        AIRY_FREE(ctx->resources[i].resource.mime_type);
    }
    AIRY_FREE(ctx->resources);

    for (size_t i = 0; i < ctx->template_count; i++) {
        AIRY_FREE(ctx->resource_templates[i].uri_template);
        AIRY_FREE(ctx->resource_templates[i].name);
        AIRY_FREE(ctx->resource_templates[i].description);
        AIRY_FREE(ctx->resource_templates[i].mime_type);
    }
    AIRY_FREE(ctx->resource_templates);

    for (size_t i = 0; i < ctx->prompt_count; i++) {
        AIRY_FREE(ctx->prompts[i].prompt.name);
        AIRY_FREE(ctx->prompts[i].prompt.description);
        AIRY_FREE(ctx->prompts[i].prompt.arguments_schema_json);
    }
    AIRY_FREE(ctx->prompts);

    AIRY_FREE(ctx->config.server_name);
    AIRY_FREE(ctx->config.server_version);

    __builtin_memset(ctx, 0, sizeof(mcp_v1_context_t));
    return 0;
}

static int mcp_adapter_encode(void *context, const void *msg, void **encoded, size_t *size)
{
    if (!context || !msg || !encoded || !size) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_encode: failed");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    const unified_message_t *umsg = (const unified_message_t *)msg;

    char *response_json = NULL;
    int result =
        mcp_v1_route_request(ctx, umsg->endpoint, (const char *)umsg->payload, &response_json);
    if (result != 0 || !response_json) {
        AIRY_LOG_ERROR("route_request failed in encode: endpoint=%s, result=%d", umsg->endpoint, result);
        *encoded = NULL;
        *size = 0;
        return result;
    }

    *encoded = response_json;
    *size = strlen(response_json);
    return 0;
}

static int mcp_adapter_decode(void *context, const void *data, size_t data_size, void *out_msg)
{
    if (!context || !data || !out_msg) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_decode: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (data_size == 0) {
        AIRY_LOG_WARN("decode called with zero data_size");
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    unified_message_t *msg = (unified_message_t *)out_msg;

    char *input_copy = AIRY_MALLOC(data_size + 1);
    if (!input_copy) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }
    __builtin_memcpy(input_copy, data, data_size);
    input_copy[data_size] = '\0';

    char *method = "tools/call";
    bool method_allocated = false;
    char *method_start = strstr(input_copy, "\"method\"");
    if (method_start) {
        method_start = strchr(method_start + 8, '"');
        if (method_start) {
            method_start++;
            char *method_end = strchr(method_start, '"');
            if (method_end) {
                size_t method_len = (size_t)(method_end - method_start);
                char *method_buf = AIRY_MALLOC(method_len + 1);
                if (method_buf) {
                    __builtin_memcpy(method_buf, method_start, method_len);
                    method_buf[method_len] = '\0';
                    method = method_buf;
                    method_allocated = true;
                }
            }
        }
    }

    char *response_json = NULL;
    int result = mcp_v1_route_request(ctx, method, input_copy, &response_json);
    if (method_allocated) {
        AIRY_FREE((void *)method);
        method = NULL;
    }
    AIRY_FREE(input_copy);
    input_copy = NULL;

    if (result == 0 && response_json) {
        msg->payload = response_json;
        msg->payload_size = strlen(response_json);
        msg->protocol = AIRY_PROTOCOL_MCP;
        msg->direction = DIRECTION_RESPONSE;
        msg->timestamp = (uint64_t)time(NULL);
    }

    return result;
}

static int mcp_adapter_connect(void *context, const char *address)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_connect: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (address) {
        char *old_name = (char *)ctx->config.server_name;
        ctx->config.server_name = AIRY_STRDUP(address);
        AIRY_FREE(old_name);
    }
    if (ctx->transport) {
        mcp_transport_config_t tcfg;
        AIRY_MEMSET(&tcfg, 0, sizeof(tcfg));
        tcfg.type = MCP_TRANSPORT_HTTP_SSE;
        tcfg.config.http.base_url = address;
        tcfg.config.http.sse_endpoint = "/sse";
        tcfg.config.http.post_endpoint = "/message";
        tcfg.read_timeout_ms = (uint32_t)ctx->config.default_timeout_ms;
        tcfg.write_timeout_ms = (uint32_t)ctx->config.default_timeout_ms;
        return mcp_transport_start(ctx->transport);
    }
    return 0;
}

static int mcp_adapter_disconnect(void *context)
{
    if (!context) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_disconnect: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (ctx->config.server_name) {
        AIRY_FREE((void *)ctx->config.server_name);
        ctx->config.server_name = NULL;
    }
    return 0;
}

static int mcp_adapter_is_connected(void *context)
{
    if (!context)
        return 0;
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (ctx->transport) {
        return mcp_transport_get_state(ctx->transport) == MCP_TRANSPORT_CONNECTED ? 1 : 0;
    }
    return (ctx->tool_count > 0 || ctx->resource_count > 0) ? 1 : 0;
}

static int mcp_adapter_send(void *context, const void *data, size_t size)
{
    if (!context || !data) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_send: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (!ctx->transport) {
        AIRY_LOG_WARN("send called but no transport configured");
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    return mcp_transport_send(ctx->transport, (const char *)data, size);
}

static int mcp_adapter_receive(void *context, void **data, size_t *size)
{
    if (!context || !data || !size) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_receive: failed");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    if (!ctx->transport) {
        AIRY_LOG_WARN("receive called but no transport configured");
        return AIRY_ERR_INVALID_PARAM;
    }
    char *msg = NULL;
    size_t msg_len = 0;
    int ret = mcp_transport_receive(ctx->transport, &msg, &msg_len, ctx->config.default_timeout_ms);
    if (ret == 0 && msg) {
        *data = msg;
        *size = msg_len;
    }
    return ret;
}

static int mcp_adapter_receive_adapter(void *context, void **data, size_t *size,
                                       uint32_t timeout_ms)
{
    (void)timeout_ms;
    return mcp_adapter_receive(context, data, size);
}

static int mcp_adapter_get_stats(void *context, char *stats_json, size_t max_size)
{
    (void)context;
    if (!stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }
    int written = snprintf(stats_json, max_size,
                           "{\"adapter_version\":\"%s\",\"protocol\":\"mcp\"}", MCP_V1_VERSION);
    return (written >= 0 && (size_t)written < max_size) ? 0 : -2;
}

static int mcp_adapter_handle_request(void *context, const void *req, void **resp)
{
    if (!context || !req || !resp) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_handle_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    mcp_v1_context_t *ctx = (mcp_v1_context_t *)context;
    const unified_message_t *msg = (const unified_message_t *)req;

    const char *method = msg->method[0] ? msg->method : "tools/call";
    const char *params = (const char *)(msg->payload ? msg->payload : "{}");

    char *response_json = NULL;
    int result = mcp_v1_route_request(ctx, method, params, &response_json);

    if (result == 0 && response_json) {
        *resp = response_json;
    } else {
        AIRY_FREE(response_json);
        response_json = NULL;
        *resp = AIRY_STRDUP("{\"error\":\"request failed\"}");
        result = -1;
    }
    return result;
}

static int mcp_adapter_get_version(void *context, char *buf, size_t max_size)
{
    (void)context;
    if (!buf || max_size == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_adapter_get_version: failed");
        return AIRY_ERR_UNKNOWN;
    }
    size_t len = strlen(MCP_V1_VERSION);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, MCP_V1_VERSION, len);
    buf[len] = '\0';
    return 0;
}

static uint32_t mcp_adapter_capabilities(void *context)
{
    (void)context;
    return (uint32_t)(MCP_CAP_TOOLS | MCP_CAP_RESOURCES | MCP_CAP_PROMPTS | MCP_CAP_LOGGING |
                      MCP_CAP_SAMPLING);
}

/* P0-01 fix: provide a static default context so mcp_adapter_init(NULL) works.
  * gateway_d/src/main.c:227 calls mcp_adapter->init(mcp_adapter->context);
  * a NULL context made init return AIRY_ERR_UNKNOWN, printing a daemon-startup
  * warning. This static default lets init complete (init creates a new context
  * via mcp_v1_context_create() and memcpy over this static object).
  *  */
static mcp_v1_context_t s_mcp_default_context = {0};

static protocol_adapter_t mcp_v1_adapter_internal = {.type = AIRY_PROTOCOL_MCP,
                                                     .name = "MCP v1.0 Protocol Adapter",
                                                     .version = MCP_V1_VERSION,
                                                     .description =
                                                         "Model Context Protocol v1.0 adapter",
                                                     .init = mcp_adapter_init,
                                                     .destroy = mcp_adapter_destroy,
                                                     .encode = mcp_adapter_encode,
                                                     .decode = mcp_adapter_decode,
                                                     .connect = mcp_adapter_connect,
                                                     .disconnect = mcp_adapter_disconnect,
                                                     .is_connected = mcp_adapter_is_connected,
                                                     .send = mcp_adapter_send,
                                                     .receive = mcp_adapter_receive_adapter,
                                                     .handle_request = mcp_adapter_handle_request,
                                                     .get_version = mcp_adapter_get_version,
                                                     .capabilities = mcp_adapter_capabilities,
                                                     .get_stats = mcp_adapter_get_stats,
                                                     .context = &s_mcp_default_context,
                                                     .user_data = NULL};

const protocol_adapter_t *mcp_v1_get_adapter(void)
{
    return &mcp_v1_adapter_internal;
}
