// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_extension_framework.c
 * @brief Protocol Extension Framework implementation.
 *
 * This file keeps framework create/destroy, the global singleton and the
 * built-in protocol adapter wrappers (proto_ext_get_framework_adapter /
 * proto_ext_get_global_instance). Extension registration, the middleware
 * pipeline, message routing and config loading live in the corresponding
 * protocol_extension_framework_registry.c / _middleware.c / _route.c / _config.c.
 */

#define LOG_TAG "protocol_extension_framework"

#include "protocol_extension_framework.h"
#include "protocol_extension_framework_internal.h"

#include "airy_memory.h"
#include "types.h"

#include <stdio.h>
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t current_time_ms(void)
{
    return (uint64_t)time(NULL) * 1000;
}

proto_ext_framework_t *proto_ext_framework_create(void)
{
    proto_ext_framework_t *fw = AIRY_CALLOC(1, sizeof(proto_ext_framework_t));
    if (!fw)
        return NULL;

    fw->adapter_capacity = 16;
    fw->adapters = AIRY_CALLOC(fw->adapter_capacity, sizeof(proto_ext_adapter_entry_t));
    fw->adapter_count = 0;

    fw->middleware_capacity = 16;
    fw->middlewares = AIRY_CALLOC(fw->middleware_capacity, sizeof(proto_middleware_t));
    fw->middleware_count = 0;

    fw->total_messages = 0;

    return fw;
}

void proto_ext_framework_destroy(proto_ext_framework_t *fw)
{
    if (!fw)
        return;

    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (fw->adapters[i].state >= PROTO_EXT_STATE_INITIALIZED &&
            fw->adapters[i].callbacks.on_unload) {
            fw->adapters[i].callbacks.on_unload(fw->adapters[i].adapter_context);
        }
    }
    AIRY_FREE(fw->adapters);
    AIRY_FREE(fw->middlewares);
    AIRY_FREE(fw);
}

static proto_ext_framework_t *g_framework_instance = NULL;

static int fw_adapter_init(void *ctx)
{
    if (!g_framework_instance) {
        g_framework_instance = proto_ext_framework_create();
    }
    return 0;
}

static int fw_adapter_destroy(void *ctx)
{
    if (g_framework_instance) {
        proto_ext_framework_destroy(g_framework_instance);
        g_framework_instance = NULL;
    }
    return 0;
}

static int fw_adapter_encode(void *ctx, const void *msg, void **out_data, size_t *out_size)
{
    if (!msg || !out_data || !out_size) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_encode: failed");
        return AIRY_ERR_UNKNOWN;
    }
    unified_message_t *umsg = (unified_message_t *)msg;
    size_t in_len =
        umsg->payload_size ? umsg->payload_size : (umsg->payload ? strlen(umsg->payload) : 0);
    if (in_len == 0) {
        *out_data = NULL;
        *out_size = 0;
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    }

    *out_data = AIRY_MALLOC(in_len);
    if (!*out_data)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
    __builtin_memcpy(*out_data, umsg->payload ? umsg->payload : "", in_len);
    *out_size = in_len;
    return 0;
}

static int fw_adapter_decode(void *ctx, const void *data, size_t size, void *out_msg)
{
    if (!data || !out_msg || size == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_decode: failed");
        return AIRY_ERR_UNKNOWN;
    }

    unified_message_t *msg = (unified_message_t *)out_msg;
    AIRY_MEMSET(msg, 0, sizeof(*msg));
    msg->payload = AIRY_MALLOC(size + 1);
    if (!msg->payload)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
    __builtin_memcpy((void *)msg->payload, data, size);
    ((char *)msg->payload)[size] = '\0';
    msg->payload_size = size;
    return 0;
}

static int fw_adapter_is_connected(void *ctx)
{
    return g_framework_instance != NULL ? 1 : 0;
}

static int fw_adapter_get_stats(void *ctx, char *stats_json, size_t max_size)
{
    if (!stats_json || max_size < 64) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }
    int written = snprintf(stats_json, max_size,
                           "{\"adapter\":\"protocol_extension_framework\",\"status\":\"active\"}");
    return (written >= 0 && (size_t)written < max_size) ? 0 : -2;
}

static int fw_adapter_connect(void *ctx, const char *endpoint)
{
    if (!endpoint) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_connect: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_framework_instance)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    for (size_t i = 0; i < g_framework_instance->adapter_count; i++) {
        if (g_framework_instance->adapters[i].state == PROTO_EXT_STATE_INITIALIZED) {
            g_framework_instance->adapters[i].state = PROTO_EXT_STATE_RUNNING;
            g_framework_instance->adapters[i].last_activity_ms = current_time_ms();
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
}

static int fw_adapter_disconnect(void *ctx)
{
    if (!g_framework_instance)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    for (size_t i = 0; i < g_framework_instance->adapter_count; i++) {
        if (g_framework_instance->adapters[i].state == PROTO_EXT_STATE_RUNNING) {
            g_framework_instance->adapters[i].state = PROTO_EXT_STATE_LOADED;
        }
    }
    return 0;
}

static int fw_adapter_send(void *ctx, const void *data, size_t size)
{
    if (!data || size == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_send: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_framework_instance)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    for (size_t i = 0; i < g_framework_instance->adapter_count; i++) {
        if (g_framework_instance->adapters[i].state == PROTO_EXT_STATE_RUNNING) {
            g_framework_instance->adapters[i].messages_processed++;
            g_framework_instance->adapters[i].last_activity_ms = current_time_ms();
            g_framework_instance->total_messages++;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
}

static int fw_adapter_receive(void *ctx, void **data, size_t *size, uint32_t timeout_ms)
{
    if (!data || !size) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__,
                         "fw_adapter_receive: timeout");
        return AIRY_ERR_TIMEOUT;
    }
    if (!g_framework_instance)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    *data = NULL;
    *size = 0;
    for (size_t i = 0; i < g_framework_instance->adapter_count; i++) {
        if (g_framework_instance->adapters[i].state == PROTO_EXT_STATE_RUNNING) {
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
}

static int fw_adapter_handle_request(void *ctx, const void *req, void **resp)
{
    if (!req || !resp) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "fw_adapter_handle_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_framework_instance)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    size_t running = 0;
    for (size_t i = 0; i < g_framework_instance->adapter_count; i++) {
        if (g_framework_instance->adapters[i].state == PROTO_EXT_STATE_RUNNING) {
            g_framework_instance->adapters[i].messages_processed++;
            g_framework_instance->total_messages++;
            running++;
        }
    }

    char stats_json[512];
    snprintf(stats_json, sizeof(stats_json),
             "{\"framework\":\"extension\","
             "\"adapters_total\":%zu,"
             "\"adapters_running\":%zu,"
             "\"messages_processed\":%llu,"
             "\"active_connections\":0}",
             g_framework_instance->adapter_count, running,
             (unsigned long long)g_framework_instance->total_messages);

    *resp = AIRY_STRDUP(stats_json);
    return *resp ? 0 : -1;
}

static int fw_adapter_get_version(void *ctx, char *buf, size_t max_size)
{
    if (!buf || max_size == 0)
        return AIRY_ENOMEM;
    const char *ver = "1.0.0";
    size_t len = strlen(ver);
    if (len >= max_size)
        len = max_size - 1;
    __builtin_memcpy(buf, ver, len);
    buf[len] = '\0';
    return 0;
}

static uint32_t fw_adapter_capabilities(void *ctx)
{
    return (uint32_t)(PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING);
}

static protocol_adapter_t proto_ext_framework_adapter_internal = {
    .type = PROTOCOL_CUSTOM,
    .name = "Protocol Extension Framework Adapter",
    .version = "0.1.0",
    .description = "Built-in protocol extension framework adapter",
    .init = fw_adapter_init,
    .destroy = fw_adapter_destroy,
    .encode = fw_adapter_encode,
    .decode = fw_adapter_decode,
    .connect = fw_adapter_connect,
    .disconnect = fw_adapter_disconnect,
    .is_connected = fw_adapter_is_connected,
    .send = fw_adapter_send,
    .receive = fw_adapter_receive,
    .handle_request = fw_adapter_handle_request,
    .get_version = fw_adapter_get_version,
    .capabilities = fw_adapter_capabilities,
    .get_stats = fw_adapter_get_stats,
    .context = NULL,
    .user_data = NULL};

const protocol_adapter_t *proto_ext_get_framework_adapter(void)
{
    return &proto_ext_framework_adapter_internal;
}

proto_ext_framework_t *proto_ext_get_global_instance(void)
{
    if (!g_framework_instance) {
        g_framework_instance = proto_ext_framework_create();
    }
    return g_framework_instance;
}
