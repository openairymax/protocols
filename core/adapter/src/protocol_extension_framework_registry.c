// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_extension_framework_registry.c
 * @brief Protocol extension framework registration, lifecycle and query domain.
 *
 * Single responsibility: extension register/unregister, load/unload/start/stop
 * state machine, adapter statistics and list/capability queries.
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

int proto_ext_register(proto_ext_framework_t *fw, const proto_ext_descriptor_t *descriptor,
                       const proto_ext_callbacks_t *callbacks)
{
    if (!fw || !descriptor || !callbacks) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_register: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (fw->adapter_count >= PROTO_EXT_MAX_ADAPTERS)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, descriptor->name) == 0) {
            AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
        }
    }

    if (fw->adapter_count >= fw->adapter_capacity) {
        size_t new_cap = fw->adapter_capacity * 2;
        proto_ext_adapter_entry_t *new_adapters =
            AIRY_REALLOC(fw->adapters, new_cap * sizeof(proto_ext_adapter_entry_t));
        if (!new_adapters)
            AIRY_ERROR(AIRY_ERR_OUT_OF_MEMORY, "out of memory");
        fw->adapters = new_adapters;
        fw->adapter_capacity = new_cap;
    }

    proto_ext_adapter_entry_t *entry = &fw->adapters[fw->adapter_count];
    AIRY_MEMSET(entry, 0, sizeof(*entry));
    __builtin_memcpy(&entry->descriptor, descriptor, sizeof(proto_ext_descriptor_t));
    __builtin_memcpy(&entry->callbacks, callbacks, sizeof(proto_ext_callbacks_t));
    entry->adapter_context = NULL;
    entry->state = PROTO_EXT_STATE_UNLOADED;
    entry->error_count = 0;
    entry->last_activity_ms = 0;
    entry->messages_processed = 0;
    entry->registered = true;
    fw->adapter_count++;

    return 0;
}

int proto_ext_unregister(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_unregister: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            if (fw->adapters[i].state >= PROTO_EXT_STATE_RUNNING) {
                proto_ext_stop(fw, name);
            }
            if (fw->adapters[i].state >= PROTO_EXT_STATE_LOADED) {
                proto_ext_unload(fw, name);
            }
            __builtin_memmove(&fw->adapters[i], &fw->adapters[i + 1],
                              (fw->adapter_count - i - 1) * sizeof(proto_ext_adapter_entry_t));
            fw->adapter_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_load(proto_ext_framework_t *fw, const char *name, const char *config_json)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "proto_ext_load: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            if (fw->adapters[i].state != PROTO_EXT_STATE_UNLOADED)
                AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

            if (fw->adapters[i].callbacks.on_load) {
                int rc = fw->adapters[i].callbacks.on_load(&fw->adapters[i].adapter_context);
                if (rc != 0) {
                    fw->adapters[i].state = PROTO_EXT_STATE_ERROR;
                    fw->adapters[i].error_count++;
                    return rc;
                }
            }

            if (fw->adapters[i].callbacks.on_init && config_json) {
                int rc =
                    fw->adapters[i].callbacks.on_init(fw->adapters[i].adapter_context, config_json);
                if (rc != 0) {
                    fw->adapters[i].state = PROTO_EXT_STATE_ERROR;
                    fw->adapters[i].error_count++;
                    return rc;
                }
            }

            fw->adapters[i].state = PROTO_EXT_STATE_LOADED;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_unload(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_unload: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            if (fw->adapters[i].callbacks.on_unload) {
                fw->adapters[i].callbacks.on_unload(fw->adapters[i].adapter_context);
            }
            fw->adapters[i].adapter_context = NULL;
            fw->adapters[i].state = PROTO_EXT_STATE_UNLOADED;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_start(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "proto_ext_start: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            if (fw->adapters[i].state < PROTO_EXT_STATE_LOADED) {
                int rc = proto_ext_load(fw, name, NULL);
                if (rc != 0)
                    return rc;
            }
            if (fw->adapters[i].callbacks.on_start) {
                int rc = fw->adapters[i].callbacks.on_start(fw->adapters[i].adapter_context);
                if (rc != 0) {
                    fw->adapters[i].state = PROTO_EXT_STATE_ERROR;
                    return rc;
                }
            }
            fw->adapters[i].state = PROTO_EXT_STATE_RUNNING;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_stop(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "proto_ext_stop: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            if (fw->adapters[i].callbacks.on_stop) {
                int rc = fw->adapters[i].callbacks.on_stop(fw->adapters[i].adapter_context);
                if (rc != 0)
                    return rc;
            }
            fw->adapters[i].state = PROTO_EXT_STATE_LOADED;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_get_adapter_stats(proto_ext_framework_t *fw, const char *name,
                                proto_ext_stats_t *stats)
{
    if (!fw || !name || !stats) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_get_adapter_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            AIRY_STRNCPY_TERM(stats->name, fw->adapters[i].descriptor.name, PROTO_EXT_MAX_NAME_LEN);
            stats->state = fw->adapters[i].state;
            stats->error_count = fw->adapters[i].error_count;
            stats->last_activity_ms = fw->adapters[i].last_activity_ms;
            stats->messages_processed = fw->adapters[i].messages_processed;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_list_adapters(proto_ext_framework_t *fw, char **names_json)
{
    if (!fw || !names_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_list_adapters: failed");
        return AIRY_ERR_UNKNOWN;
    }
    size_t buf_size = 4096 + fw->adapter_count * 128;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    size_t offset = snprintf(buf, buf_size, "{\"adapters\":[");
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        const char *state_str = "unknown";
        switch (fw->adapters[i].state) {
        case PROTO_EXT_STATE_UNLOADED:
            state_str = "unloaded";
            break;
        case PROTO_EXT_STATE_LOADED:
            state_str = "loaded";
            break;
        case PROTO_EXT_STATE_INITIALIZED:
            state_str = "initialized";
            break;
        case PROTO_EXT_STATE_RUNNING:
            state_str = "running";
            break;
        case PROTO_EXT_STATE_ERROR:
            state_str = "error";
            break;
        case PROTO_EXT_STATE_DISABLED:
            state_str = "disabled";
            break;
        }
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"name\":\"%s\",\"version\":\"%s\",\"state\":\"%s\",\"type\":%d}",
                           fw->adapters[i].descriptor.name, fw->adapters[i].descriptor.version,
                           state_str, fw->adapters[i].descriptor.protocol_type);
    }
    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *names_json = buf;
    return 0;
}

int proto_ext_list_capabilities(proto_ext_framework_t *fw, char **caps_json)
{
    if (!fw || !caps_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_list_capabilities: failed");
        return AIRY_ERR_UNKNOWN;
    }

    uint32_t all_caps = 0;
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (fw->adapters[i].state >= PROTO_EXT_STATE_LOADED) {
            all_caps |= fw->adapters[i].descriptor.capabilities;
        }
    }

    size_t buf_size = 2048;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    size_t offset = snprintf(buf, buf_size, "{\"capabilities\":[");

    bool first = true;
    const struct {
        uint32_t bit;
        const char *name;
    } cap_names[] = {
        {PROTO_CAP_REQUEST_RESPONSE, "request_response"},
        {PROTO_CAP_STREAMING, "streaming"},
        {PROTO_CAP_BIDIRECTIONAL, "bidirectional"},
        {0x0008, "binary"},
        {0x0010, "compression"},
        {0x0020, "encryption"},
        {PROTO_CAP_AUTHENTICATION, "authentication"},
        {PROTO_CAP_AGENT_DISCOVERY, "discovery"},
        {0x0080, "delegation"},
        {PROTO_CAP_NEGOTIATION, "negotiation"},
        {0x2000, "consensus"},
        {PROTO_CAP_FUNCTION_CALLING, "tool_use"},
        {0x4000, "vision"},
        {0x8000, "extended_thinking"},
        {0x10000, "code_execution"},
        {0x20000, "human_in_loop"},
        {0x40000, "memory"},
        {0x80000, "rag"},
        {PROTO_CAP_CUSTOM, "multi_agent"},
        {0x100000, "cluster"},
        {0x0100, "embedding"},
        {0x0200, "token_counting"},
        {0x0400, "prompt_caching"},
        {0x10000, "safety_filter"},
    };

    for (size_t c = 0; c < sizeof(cap_names) / sizeof(cap_names[0]); c++) {
        if (all_caps & cap_names[c].bit) {
            if (!first)
                offset += snprintf(buf + offset, buf_size - offset, ",");
            offset += snprintf(buf + offset, buf_size - offset, "\"%s\"", cap_names[c].name);
            first = false;
        }
    }

    snprintf(buf + offset, buf_size - offset, "],\"raw_flags\":%u}", all_caps);
    *caps_json = buf;
    return 0;
}

int proto_ext_find_by_capability(proto_ext_framework_t *fw, uint32_t capability,
                                 char ***adapter_names, size_t *count)
{
    if (!fw || !adapter_names || !count) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_find_by_capability: failed");
        return AIRY_ERR_UNKNOWN;
    }

    size_t found = 0;
    char **results = AIRY_CALLOC(fw->adapter_count, sizeof(char *));
    if (!results)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (fw->adapters[i].descriptor.capabilities & capability) {
            results[found++] = AIRY_STRDUP(fw->adapters[i].descriptor.name);
        }
    }

    *adapter_names = results;
    *count = found;
    return 0;
}

proto_ext_state_t proto_ext_get_state(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name)
        return PROTO_EXT_STATE_UNLOADED;
    for (size_t i = 0; i < fw->adapter_count; i++) {
        if (strcmp(fw->adapters[i].descriptor.name, name) == 0) {
            return fw->adapters[i].state;
        }
    }
    return PROTO_EXT_STATE_UNLOADED;
}
