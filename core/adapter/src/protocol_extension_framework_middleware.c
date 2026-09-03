// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_extension_framework_middleware.c
 * @brief Protocol extension framework middleware hook pipeline domain.
 *
 * Single responsibility: middleware register/remove/enable/disable and
 * priority-ordered hook pipeline execution.
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

int proto_ext_add_middleware(proto_ext_framework_t *fw, const char *name,
                             proto_middleware_fn middleware, proto_ext_priority_t priority,
                             void *user_data)
{
    if (!fw || !name || !middleware) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_add_middleware: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (fw->middleware_count >= PROTO_EXT_MAX_MIDDLEWARE)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    if (fw->middleware_count >= fw->middleware_capacity) {
        size_t new_cap = fw->middleware_capacity * 2;
        proto_middleware_t *new_mw =
            AIRY_REALLOC(fw->middlewares, new_cap * sizeof(proto_middleware_t));
        if (!new_mw)
            AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
        fw->middlewares = new_mw;
        fw->middleware_capacity = new_cap;
    }

    proto_middleware_t *mw = &fw->middlewares[fw->middleware_count];
    AIRY_STRNCPY_TERM(mw->name, name, PROTO_EXT_MAX_NAME_LEN);
    mw->name[PROTO_EXT_MAX_NAME_LEN - 1] = '\0';
    mw->process = middleware;
    mw->priority = priority;
    mw->user_data = user_data;
    mw->enabled = true;
    fw->middleware_count++;

    for (size_t i = fw->middleware_count - 1; i > 0; i--) {
        if (fw->middlewares[i].priority > fw->middlewares[i - 1].priority) {
            proto_middleware_t tmp = fw->middlewares[i];
            fw->middlewares[i] = fw->middlewares[i - 1];
            fw->middlewares[i - 1] = tmp;
        }
    }

    return 0;
}

int proto_ext_remove_middleware(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_remove_middleware: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->middleware_count; i++) {
        if (strcmp(fw->middlewares[i].name, name) == 0) {
            __builtin_memmove(&fw->middlewares[i], &fw->middlewares[i + 1],
                              (fw->middleware_count - i - 1) * sizeof(proto_middleware_t));
            fw->middleware_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_enable_middleware(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_enable_middleware: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->middleware_count; i++) {
        if (strcmp(fw->middlewares[i].name, name) == 0) {
            fw->middlewares[i].enabled = true;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_disable_middleware(proto_ext_framework_t *fw, const char *name)
{
    if (!fw || !name) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_disable_middleware: failed");
        return AIRY_ERR_UNKNOWN;
    }
    for (size_t i = 0; i < fw->middleware_count; i++) {
        if (strcmp(fw->middlewares[i].name, name) == 0) {
            fw->middlewares[i].enabled = false;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int proto_ext_process_middleware_chain(proto_ext_framework_t *fw, const unified_message_t *request,
                                       unified_message_t *response)
{
    if (!fw || !request || !response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "proto_ext_process_middleware_chain: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < fw->middleware_count; i++) {
        if (!fw->middlewares[i].enabled)
            continue;
        int rc = fw->middlewares[i].process(request, response, fw->middlewares[i].user_data);
        if (rc != 0)
            return rc;
    }
    return 0;
}
