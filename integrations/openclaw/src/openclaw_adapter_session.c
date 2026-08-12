// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openclaw_adapter_session.c
 * @brief OpenClaw adapter session domain (session creation and closing).
 */

#define LOG_TAG "openclaw_adapter"

#include "openclaw_adapter.h"
#include "openclaw_adapter_internal.h"

#include "protocol_transformers.h"

#include <stdio.h>
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "airy_memory.h"
#include "types.h"

int openclaw_create_session(openclaw_adapter_context_t *ctx,
                            const openclaw_session_t *session_template,
                            openclaw_session_t *out_session)
{
    if (!ctx || !out_session) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_create_session: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!ctx->connected)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    static uint32_t session_counter = 0;
    session_counter++;

    AIRY_MEMSET(out_session, 0, sizeof(openclaw_session_t));

    char sid[64];
    snprintf(sid, sizeof(sid), "oc-session-%08x", session_counter);
    out_session->session_id = AIRY_STRDUP(sid);

    if (session_template) {
        out_session->agent_id =
            session_template->agent_id ? AIRY_STRDUP(session_template->agent_id) : NULL;
        out_session->modality = session_template->modality;
        out_session->security_level = session_template->security_level;
    } else {
        out_session->modality = OPENCLAW_MODALITY_TEXT;
        out_session->security_level = ctx->config.default_security_level;
    }

    out_session->created_at = (uint64_t)(time(NULL));
    out_session->last_activity = out_session->created_at;
    out_session->is_active = true;

    openclaw_session_t *new_sessions =
        (openclaw_session_t *)AIRY_REALLOC(ctx->active_sessions, (ctx->active_session_count + 1) *
                                                                     sizeof(openclaw_session_t));
    if (!new_sessions)
        return AIRY_ERR_OUT_OF_MEMORY;
    ctx->active_sessions = new_sessions;
    __builtin_memcpy(&ctx->active_sessions[ctx->active_session_count], out_session,
                     sizeof(openclaw_session_t));
    ctx->active_sessions[ctx->active_session_count].session_id =
        AIRY_STRDUP(out_session->session_id);
    ctx->active_sessions[ctx->active_session_count].agent_id =
        out_session->agent_id ? AIRY_STRDUP(out_session->agent_id) : NULL;
    ctx->active_session_count++;

    return 0;
}

int openclaw_close_session(openclaw_adapter_context_t *ctx, const char *session_id)
{
    if (!ctx || !session_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openclaw_close_session: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < ctx->active_session_count; i++) {
        if (strcmp(ctx->active_sessions[i].session_id, session_id) == 0) {
            ctx->active_sessions[i].is_active = false;
            openclaw_session_destroy(&ctx->active_sessions[i]);
            if (i < ctx->active_session_count - 1) {
                __builtin_memmove(&ctx->active_sessions[i], &ctx->active_sessions[i + 1],
                                  (ctx->active_session_count - i - 1) * sizeof(openclaw_session_t));
            }
            ctx->active_session_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}
