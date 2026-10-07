// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file a2a_v03_adapter.c
 * @brief A2A (Agent-to-Agent) Protocol v0.3 adapter implementation.
 *
 * Implements the full A2A protocol v0.3 adapter, supporting:
 * - agent/discover - agent discovery
 * - task/delegate - task delegation
 * - task/negotiate - task negotiation
 * - task/consensus - multi-agent consensus
 * - task/stream - streaming task execution
 *
 * @since 0.1.0
 */

#include "a2a_v03_adapter.h"
#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

int a2a_v03_create(a2a_config_t config, a2a_handle_t *out_handle)
{
    if (!out_handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "a2a_v03_create: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct a2a_v03_adapter_s *adapter = AIRY_CALLOC(1, sizeof(struct a2a_v03_adapter_s));
    if (!adapter) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    if (config.default_timeout_ms > 0) {
        adapter->config.default_timeout_ms = config.default_timeout_ms;
    }

    adapter->agent_count = 0;
    adapter->task_counter = 1;
    adapter->initialized = true;

    *out_handle = (a2a_handle_t)adapter;
    return 0;
}

void a2a_v03_ctx_clear(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->initialized = false;

    for (size_t i = 0; i < adapter->agent_count; i++) {
        AIRY_FREE(adapter->agents[i].capabilities_json);
        adapter->agents[i].capabilities_json = NULL;
    }
    adapter->agent_count = 0;

    for (size_t i = 0; i < adapter->task_count; i++)
        a2a_task_destroy(adapter->tasks[i]);
    adapter->task_count = 0;

    /* Releasing agents above may have dropped the entry the static agent card
     * was mirrored from; a2a_v03_get_agent_card(NULL, NULL) frees the cached
     * card's strings so no stale pointer is left behind. */
    a2a_v03_get_agent_card(NULL, NULL);
}

void a2a_v03_destroy(a2a_handle_t handle)
{
    if (!handle)
        return;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    a2a_v03_ctx_clear((a2a_v03_context_t *)adapter);
    AIRY_FREE(adapter);
}

bool a2a_v03_is_initialized(a2a_handle_t handle)
{
    if (!handle)
        return false;
    return ((struct a2a_v03_adapter_s *)handle)->initialized;
}

const char *a2a_v03_version(void)
{
    return "AgentRT-A2A/" A2A_VERSION;
}

/* ============================================================================
 * New-Style API implementations (a2a_v03_adapter.h declared functions)
 * ============================================================================ */

a2a_v03_config_t a2a_v03_config_default(void)
{
    a2a_v03_config_t cfg;
    AIRY_MEMSET(&cfg, 0, sizeof(cfg));
    cfg.capabilities = A2A_CAP_TASK_EXECUTION | A2A_CAP_STREAMING | A2A_CAP_NEGOTIATION |
                       A2A_CAP_PUSH_NOTIFICATIONS | A2A_CAP_MULTI_TURN | A2A_CAP_STATE_TRANSITION;
    cfg.max_agents = 256;
    cfg.max_tasks = 4096;
    cfg.max_message_size = 65536;
    cfg.default_timeout_ms = 60000;
    cfg.enable_negotiation = true;
    cfg.enable_streaming = true;
    cfg.enable_push_notifications = true;
    cfg.require_authentication = false;
    cfg.default_authentication = NULL;
    return cfg;
}

a2a_v03_context_t *a2a_v03_context_create(const a2a_v03_config_t *config)
{

    if (!config)
        return NULL;
    a2a_v03_config_t cfg = *config;
    a2a_handle_t handle = NULL;
    a2a_config_t legacy_cfg;
    AIRY_MEMSET(&legacy_cfg, 0, sizeof(legacy_cfg));
    legacy_cfg.max_agents = (uint32_t)cfg.max_agents;
    legacy_cfg.max_tasks = (uint32_t)cfg.max_tasks;
    legacy_cfg.default_timeout_ms = cfg.default_timeout_ms;
    if (a2a_v03_create(legacy_cfg, &handle) != 0)
        return NULL;
    return (a2a_v03_context_t *)handle;
}

void a2a_v03_context_destroy(a2a_v03_context_t *ctx)
{
    if (ctx)
        a2a_v03_destroy((a2a_handle_t)ctx);
}

/* The default context is a module-level static so the adapter table can bind
 * it at link time; destroying the adapter must not free a static shell. */
static struct a2a_v03_adapter_s s_a2a_default_context = {0};

static protocol_adapter_t a2a_v03_adapter_internal = {
    .type = AIRY_PROTOCOL_A2A,
    .name = "A2A v0.3 Protocol Adapter",
    .version = A2A_V03_VERSION,
    .description = "Agent-to-Agent Protocol v0.3 adapter",
    .init = a2a_adapter_init_cb,
    .destroy = a2a_adapter_destroy_cb,
    .encode = a2a_adapter_encode_cb,
    .decode = a2a_adapter_decode_cb,
    .connect = a2a_adapter_connect_cb,
    .disconnect = a2a_adapter_disconnect_cb,
    .is_connected = a2a_adapter_is_connected_cb,
    .send = a2a_adapter_send_cb,
    .receive = a2a_adapter_receive_cb,
    .handle_request = a2a_adapter_handle_request_cb,
    .get_version = a2a_adapter_get_version_cb,
    .capabilities = a2a_adapter_capabilities_cb,
    .get_stats = a2a_adapter_get_stats_cb,
    .context = &s_a2a_default_context,
    .user_data = NULL,
};

const protocol_adapter_t *a2a_v03_get_adapter(void)
{
    return &a2a_v03_adapter_internal;
}

size_t a2a_v03_get_agent_count(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return 0;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    return adapter->agent_count;
}

size_t a2a_v03_get_task_count(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return 0;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    return adapter->task_count;
}

uint32_t a2a_v03_get_capabilities(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return 0;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    return adapter->config.capabilities;
}

void a2a_agent_card_destroy(a2a_agent_card_t *card)
{
    if (!card)
        return;
    AIRY_FREE(card->id);
    AIRY_FREE(card->name);
    AIRY_FREE(card->description);
    AIRY_FREE(card->url);
    AIRY_FREE(card->version);
    AIRY_FREE(card->provider_name);
    AIRY_FREE(card->provider_url);
    AIRY_FREE(card->documentation_url);
    AIRY_FREE(card->authentication_schemes_json);
    AIRY_FREE(card->capabilities_json);
    if (card->skills) {
        for (size_t i = 0; i < card->skill_count; i++) {
            AIRY_FREE(card->skills[i].name);
            AIRY_FREE(card->skills[i].description);
            AIRY_FREE(card->skills[i].schema_json);
        }
        AIRY_FREE(card->skills);
    }
    /* P0-07 fix: removed AIRY_FREE(card).
     *
      * Historical bug: the old code AIRY_FREE'd the card pointer itself, but card may be
      * stack-allocated (a caller local) or static (the g_cached_card of
      * a2a_v03_get_agent_card); AIRY_FREE on a non-heap pointer gave an ASAN bad-free.
     *
      * Fix: free only the card's fields, never the card. The caller owns the card
      * pointer (when heap-allocated). This matches standard destroy patterns like cJSON_free. */
}

void a2a_task_destroy(a2a_task_t *task)
{
    if (!task)
        return;
    AIRY_FREE(task->id);
    AIRY_FREE(task->session_id);
    AIRY_FREE(task->agent_id);
    AIRY_FREE(task->description);
    AIRY_FREE(task->input_json);
    AIRY_FREE(task->output_json);
    AIRY_FREE(task->error_message);
    AIRY_FREE(task);
}

void a2a_message_destroy(a2a_message_t *msg)
{
    if (!msg)
        return;
    AIRY_FREE(msg->role);
    AIRY_FREE(msg->content_json);
    AIRY_FREE(msg->mime_type);
    AIRY_FREE(msg->file_name);
    AIRY_FREE(msg->file_data);
    AIRY_FREE(msg);
}

void a2a_negotiation_destroy(a2a_negotiation_t *neg)
{
    if (!neg)
        return;
    AIRY_FREE(neg->task_id);
    AIRY_FREE(neg->agent_id);
    AIRY_FREE(neg->terms_json);
    AIRY_FREE(neg->counter_proposal_json);
    AIRY_FREE(neg->reason);
    AIRY_FREE(neg);
}
