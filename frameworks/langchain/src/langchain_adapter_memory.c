// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file langchain_adapter_memory.c
 * @brief LangChain adapter memory management domain.
 *
 * Single responsibility: memory instance creation (langchain_create_memory),
 * memory entry append (langchain_memory_add) and memory snapshot query
 * (langchain_memory_get).
 */

#define LOG_TAG "langchain_adapter"

#include "langchain_adapter.h"
#include "langchain_adapter_internal.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int langchain_create_memory(langchain_adapter_context_t *ctx, langchain_memory_type_t type,
                            size_t max_entries, langchain_memory_t *out_memory)
{
    if (!ctx || !out_memory)
        return AIRY_ERR_NULL_POINTER;

    static uint32_t mem_counter = 0;
    mem_counter++;

    AIRY_MEMSET(out_memory, 0, sizeof(langchain_memory_t));
    char mid[64];
    snprintf(mid, sizeof(mid), "lc-mem-%08x", mem_counter);
    out_memory->id = AIRY_STRDUP(mid);
    out_memory->type = type;
    out_memory->max_entries = max_entries > 0 ? max_entries : LANGCHAIN_MAX_MEMORY_ENTRIES;
    out_memory->current_entries = 0;
    out_memory->messages = NULL;
    out_memory->message_count = 0;
    out_memory->summary = NULL;
    out_memory->last_updated = (uint64_t)(time(NULL));

    if (ctx->memory_count < LANGCHAIN_MAX_MEMORY_ENTRIES) {
        __builtin_memcpy(&ctx->memories[ctx->memory_count], out_memory, sizeof(langchain_memory_t));
        ctx->memories[ctx->memory_count].id = AIRY_STRDUP(out_memory->id);
        ctx->memory_count++;
    }

    return 0;
}

int langchain_memory_add(langchain_adapter_context_t *ctx, const char *memory_id, const char *role,
                         const char *content)
{
    if (!ctx || !memory_id || !role || !content) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "langchain_memory_add: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t m = 0; m < ctx->memory_count; m++) {
        if (strcmp(ctx->memories[m].id, memory_id) == 0) {
            langchain_memory_t *mem = &ctx->memories[m];
            if (mem->current_entries >= mem->max_entries)
                return AIRY_ERR_OVERFLOW;

            char **msgs =
                (char **)AIRY_REALLOC(mem->messages, (mem->message_count + 1) * sizeof(char *));
            if (!msgs)
                return AIRY_ERR_IO;
            mem->messages = msgs;

            char entry[1024];
            snprintf(entry, sizeof(entry), "{\"role\":\"%s\",\"content\":\"%.900s\"}", role,
                     content);
            mem->messages[mem->message_count] = AIRY_STRDUP(entry);
            mem->message_count++;
            mem->current_entries++;
            mem->last_updated = (uint64_t)(time(NULL));
            return 0;
        }
    }
    return AIRY_ERR_OUT_OF_MEMORY;
}

int langchain_memory_get(langchain_adapter_context_t *ctx, const char *memory_id,
                         langchain_memory_t *snapshot)
{
    if (!ctx || !memory_id || !snapshot)
        return AIRY_ERR_NULL_POINTER;

    for (size_t m = 0; m < ctx->memory_count; m++) {
        if (strcmp(ctx->memories[m].id, memory_id) == 0) {
            __builtin_memcpy(snapshot, &ctx->memories[m], sizeof(langchain_memory_t));
            snapshot->id = AIRY_STRDUP(ctx->memories[m].id);
            snapshot->messages =
                (char **)AIRY_CALLOC(ctx->memories[m].message_count, sizeof(char *));
            for (size_t i = 0; i < ctx->memories[m].message_count; i++)
                snapshot->messages[i] = AIRY_STRDUP(ctx->memories[m].messages[i]);
            snapshot->summary =
                ctx->memories[m].summary ? AIRY_STRDUP(ctx->memories[m].summary) : NULL;
            return 0;
        }
    }
    return AIRY_ERR_OUT_OF_MEMORY;
}
