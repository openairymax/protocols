// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_agent.c
 * @brief A2A v0.3 agent discovery and registration management (agent/discover, etc.).
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Agent Discovery
 * ============================================================================ */

int a2a_v03_register_agent(a2a_v03_context_t *ctx, const a2a_agent_card_t *card)
{
    if (!ctx || !card) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_register_agent: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    /* Duplicate-registration check: update the existing entry and merge the capabilities
      * bitmask without bumping agent_count (test contract: "duplicate should not increase count"). */
    const char *new_id = (card->id && card->id[0]) ? card->id : NULL;
    if (new_id) {
        for (size_t i = 0; i < adapter->agent_count; i++) {
            if (strcmp(adapter->agents[i].id, new_id) == 0) {

                a2a_internal_card_t *ic = &adapter->agents[i];
                AIRY_STRNCPY_TERM(ic->name, card->name ? card->name : "Unknown", sizeof(ic->name));
                AIRY_STRNCPY_TERM(ic->url, card->url ? card->url : "", sizeof(ic->url));
                if (card->capabilities_json) {
                    AIRY_STRNCPY_TERM(ic->capabilities, card->capabilities_json,
                                      sizeof(ic->capabilities));
                }
                ic->version = card->protocol_version > 0 ? card->protocol_version : 3;
                ic->available = card->available;

                ic->capabilities_mask =
                    (a2a_capability_t)((int)ic->capabilities_mask | (int)card->capabilities);
                return 0;
            }
        }
    }

    if (adapter->agent_count >= A2A_MAX_AGENTS) {
        airy_err_push_ex(AIRY_ERR_BUFFER_TOO_SMALL, __FILE__, __LINE__, __func__,
                         "capacity exceeded");
        return AIRY_ERR_BUFFER_TOO_SMALL;
    }

    a2a_internal_card_t *internal_card = &adapter->agents[adapter->agent_count];
    /* Uses the caller-provided card->id (contract: get_agent_card(card->id) hits after register).
      * If the caller gives no id, generate "agent_<n>_<counter>" as before. */
    if (new_id) {
        AIRY_STRNCPY_TERM(internal_card->id, new_id, sizeof(internal_card->id));
    } else {
        snprintf(internal_card->id, sizeof(internal_card->id), "agent_%zu_%" PRIu64,
                 adapter->agent_count + 1, adapter->task_counter++);
    }
    AIRY_STRNCPY_TERM(internal_card->name, card->name ? card->name : "Unknown",
                      sizeof(internal_card->name));
    AIRY_STRNCPY_TERM(internal_card->url, card->url ? card->url : "", sizeof(internal_card->url));

    if (card->capabilities_json) {
        AIRY_STRNCPY_TERM(internal_card->capabilities, card->capabilities_json,
                          sizeof(internal_card->capabilities));
    }
    internal_card->version = card->protocol_version > 0 ? card->protocol_version : 3;
    internal_card->available = card->available;
    internal_card->capabilities_mask = card->capabilities;

    adapter->agent_count++;
    return 0;
}

int a2a_v03_discover_agents(a2a_v03_context_t *ctx, const char *capability, const char *skill_name,
                            a2a_agent_card_t ***results, size_t *result_count)
{
    if (!ctx || !results || !result_count) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_discover_agents: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    *result_count = 0;
    *results = NULL;

    size_t matched = 0;
    a2a_agent_card_t **agent_array = NULL;

    for (size_t i = 0; i < adapter->agent_count && matched < A2A_MAX_AGENTS; i++) {
        const a2a_internal_card_t *card = &adapter->agents[i];

        if (!card->available)
            continue;
        if (capability && capability[0] != '\0') {
            if (!strstr(card->capabilities, capability))
                continue;
        }

        matched++;
    }

    if (matched > 0) {
        agent_array = (a2a_agent_card_t **)AIRY_CALLOC(matched, sizeof(a2a_agent_card_t *));
        if (!agent_array) {
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }

        size_t idx = 0;
        for (size_t i = 0; i < adapter->agent_count && idx < matched; i++) {
            const a2a_internal_card_t *card = &adapter->agents[i];
            if (!card->available)
                continue;
            if (capability && capability[0] != '\0') {
                if (!strstr(card->capabilities, capability))
                    continue;
            }

            agent_array[idx] = (a2a_agent_card_t *)AIRY_CALLOC(1, sizeof(a2a_agent_card_t));
            if (agent_array[idx]) {
                agent_array[idx]->id = AIRY_STRDUP(card->id);
                agent_array[idx]->name = AIRY_STRDUP(card->name);
                agent_array[idx]->url = AIRY_STRDUP(card->url);
                agent_array[idx]->capabilities_json = AIRY_STRDUP(card->capabilities);
                agent_array[idx]->protocol_version = card->version;
                idx++;
            }
        }
    }

    *results = agent_array;
    *result_count = matched;
    return 0;
}

const a2a_agent_card_t *a2a_v03_get_agent_card(a2a_v03_context_t *ctx, const char *agent_id)
{
    /* P0-07: static card buffer, cleaned via the (NULL, NULL) call in a2a_v03_destroy().
     *
      * Trade-off: keep the static card for API compatibility (const return, no caller free),
      * but add an explicit cleanup path to avoid ASAN leaks. */
    static a2a_agent_card_t g_cached_card = {0};

    if (!ctx && !agent_id) {
        a2a_agent_card_destroy(&g_cached_card);
        AIRY_MEMSET(&g_cached_card, 0, sizeof(g_cached_card));
        return NULL;
    }
    if (!ctx || !agent_id)
        return NULL;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized)
        return NULL;

    for (size_t i = 0; i < adapter->agent_count; i++) {
        if (strcmp(adapter->agents[i].id, agent_id) == 0) {
            /* P0-07: free the previous STRDUP'd string to avoid cumulative leaks.
              * Old bug: every call STRDUP'd without freeing the previous string, leaking over calls. */
            a2a_agent_card_destroy(&g_cached_card);
            AIRY_MEMSET(&g_cached_card, 0, sizeof(g_cached_card));
            const a2a_internal_card_t *internal = &adapter->agents[i];
            g_cached_card.id = AIRY_STRDUP(internal->id);
            g_cached_card.name = AIRY_STRDUP(internal->name);
            g_cached_card.url = AIRY_STRDUP(internal->url);
            g_cached_card.capabilities_json = AIRY_STRDUP(internal->capabilities);
            g_cached_card.protocol_version = internal->version;
            g_cached_card.capabilities = internal->capabilities_mask;
            g_cached_card.available = internal->available;
            return &g_cached_card;
        }
    }

    return NULL;
}

void a2a_free_agent_list(a2a_agent_list_t *list)
{
    if (!list)
        return;
    for (size_t i = 0; i < list->count && i < A2A_MAX_AGENTS; i++) {
        AIRY_FREE((void *)list->agents[i].id);
        AIRY_FREE((void *)list->agents[i].name);
        AIRY_FREE((void *)list->agents[i].url);
        AIRY_FREE((void *)list->agents[i].capabilities_json);
    }
    AIRY_MEMSET(list, 0, sizeof(*list));
}

int a2a_v03_unregister_agent(a2a_v03_context_t *ctx, const char *agent_id)
{
    if (!ctx || !agent_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_unregister_agent: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }
    for (size_t i = 0; i < adapter->agent_count; i++) {
        if (strcmp(adapter->agents[i].id, agent_id) == 0) {
            adapter->agents[i].name[0] = '\0';
            adapter->agents[i].url[0] = '\0';
            if (i < adapter->agent_count - 1) {
                adapter->agents[i] = adapter->agents[adapter->agent_count - 1];
            }
            adapter->agent_count--;
            return 0;
        }
    }
    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}
