// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_extension_framework_config.c
 * @brief Protocol extension framework config-loading domain.
 *
 * Single responsibility: parse extension descriptors from JSON config text
 * and bulk register/load them (proto_ext_load_from_config), plus JSON
 * key-value extraction helpers.
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

static char *json_extract_string(const char *json, const char *key)
{
    if (!json || !key)
        return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p)
        return NULL;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == ':'))
        p++;
    if (*p != '"')
        return NULL;
    p++;
    const char *end = strchr(p, '"');
    if (!end)
        return NULL;
    size_t len = end - p;
    char *result = AIRY_MALLOC(len + 1);
    __builtin_memcpy(result, p, len);
    result[len] = '\0';
    return result;
}

static int json_extract_int(const char *json, const char *key, int default_val)
{
    char *s = json_extract_string(json, key);
    if (s) {
        int v = (int)strtol(s, NULL, 10);
        AIRY_FREE(s);
        return v;
    }
    return default_val;
}

int proto_ext_load_from_config(proto_ext_framework_t *fw, const char *config_json)
{
    if (!fw || !config_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_load_from_config: failed");
        return AIRY_ERR_UNKNOWN;
    }

    const char *adapters_start = strstr(config_json, "\"adapters\"");
    if (!adapters_start) {
        adapters_start = strstr(config_json, "\"extensions\"");
        if (!adapters_start)
            AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
    }

    const char *array_start = strchr(adapters_start, '[');
    if (!array_start)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    int loaded_count = 0;
    const char *p = array_start + 1;
    while (*p && *p != ']') {
        const char *obj_start = strchr(p, '{');
        if (!obj_start)
            break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end)
            break;

        size_t obj_len = obj_end - obj_start + 1;
        char *obj_buf = AIRY_MALLOC(obj_len + 1);
        __builtin_memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        char *name = json_extract_string(obj_buf, "name");
        char *version = json_extract_string(obj_buf, "version");
        char *desc = json_extract_string(obj_buf, "description");
        char *author = json_extract_string(obj_buf, "author");
        int proto_type = json_extract_int(obj_buf, "protocol_type", PROTOCOL_CUSTOM);
        uint32_t caps = (uint32_t)json_extract_int(obj_buf, "capabilities", 0);
        int priority = json_extract_int(obj_buf, "priority", 50);

        if (name) {
            proto_ext_descriptor_t desc_struct = {.protocol_type = proto_type,
                                                  .capabilities = caps,
                                                  .priority = priority,
                                                  .hot_loadable = true};
            AIRY_STRNCPY_TERM(desc_struct.name, name, PROTO_EXT_MAX_NAME_LEN);
            if (version)
                AIRY_STRNCPY_TERM(desc_struct.version, version, PROTO_EXT_MAX_VERSION_LEN);
            else {
                AIRY_STRNCPY_TERM(desc_struct.version, "1.0.0", PROTO_EXT_MAX_VERSION_LEN);
            }
            if (desc)
                AIRY_STRNCPY_TERM(desc_struct.description, desc, sizeof(desc_struct.description));
            else {
                AIRY_STRNCPY_TERM(desc_struct.description, "Loaded from config",
                                  sizeof(desc_struct.description));
            }
            if (author)
                AIRY_STRNCPY_TERM(desc_struct.author, author, sizeof(desc_struct.author));

            proto_ext_callbacks_t empty_cbs = {0};

            int rc = proto_ext_register(fw, &desc_struct, &empty_cbs);
            if (rc == 0) {
                proto_ext_load(fw, name, obj_buf);
                loaded_count++;
            }
        }

        AIRY_FREE(name);
        AIRY_FREE(version);
        AIRY_FREE(desc);
        AIRY_FREE(author);
        AIRY_FREE(obj_buf);
        p = obj_end + 1;
        while (*p && (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r'))
            p++;
    }

    return loaded_count;
}
