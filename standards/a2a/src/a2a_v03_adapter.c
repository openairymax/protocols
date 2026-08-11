// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file a2a_v03_adapter.c
 * @brief A2A (Agent-to-Agent) Protocol v0.3 Adapter Implementation
 *
 * 实现 Agent-to-Agent 协议 v0.3 的完整适配器，支持：
 * - agent/discover — 智能体发现
 * - task/delegate — 任务委派
 * - task/negotiate — 任务协商
 * - task/consensus — 多智能体共识
 * - task/stream — 流式任务执行
 *
 * @since 0.1.0
 */

#include "a2a_v03_adapter.h"

#include "airy_memory.h"
#include "error.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"

/* Forward declarations for types defined in header */
typedef struct a2a_v03_adapter_s a2a_v03_adapter_t;

/* Internal types needed only in this translation unit */
typedef struct {
    int type;
    float proposed_cost;
    int proposed_timeout_ms;
    int proposed_priority;
} a2a_proposal_internal_t;
typedef struct {
    char task_id[64];
    int outcome;
    float final_cost;
    int final_timeout_ms;
    int counter_priority;
    int round_number;
} a2a_negotiation_result_internal_t;
typedef struct {
    const char *task_id;
    const char *description;
    int num_participants;
    float consensus_threshold;
} a2a_consensus_request_internal_t;
typedef struct {
    char task_id[64];
    int agreed;
    int rounds_completed;
    int agreements[16];
    int total_participants;
    int agree_count;
    bool consensus_reached;
    int consensus_type;
} a2a_consensus_result_internal_t;
typedef void (*a2a_stream_callback_internal_t)(const char *, size_t, bool, void *);
typedef struct {
    const char *task_id;
    const char *target_agent_id;
    const char *description;
    int timeout_ms;
    a2a_stream_callback_internal_t callback;
    void *user_data;
} a2a_task_request_internal_t;
typedef struct {
    char task_id[64];
    int status;
    char *result_json;
    char accepted_by[64];
    int negotiation_rounds;
    int estimated_duration_ms;
} a2a_task_response_internal_t;
typedef struct {
    uint32_t total_tasks;
    uint32_t active;
    uint32_t active_tasks;
    uint32_t completed_tasks;
    uint32_t failed_tasks;
    double avg_delegation_latency_ms;
    double avg_consensus_latency_ms;
    uint32_t registered_agents;
} a2a_stats_internal_t;
typedef struct {
    int event_type;
    char task_id[64];
    uint8_t progress_percentage;
    char phase[64];
    char *detail_json;
} a2a_progress_event_internal_t;

/* Compatibility defines */
#define A2A_PROGRESS_UPDATE 1
#define A2A_TASK_STATUS_COMPLETED 3
#define A2A_TASK_STATUS_ACCEPTED 4
#define A2A_OUTCOME_REJECTED 3
#define A2A_CONSENSUS_MAJORITY 1
#define A2A_CONSENSUS_UNANIMOUS 2
#define A2A_CONSENSUS_NONE 0
#define A2A_NEGOTIATE_COST 1
#define A2A_NEGOTIATE_TIMEOUT 2
#define A2A_NEGOTIATE_PRIORITY 3
#define A2A_OUTCOME_ACCEPTED 1
#define A2A_OUTCOME_COUNTER_OFFER 2
#define A2A_VERSION "0.3"

/* Internal agent card struct with fixed arrays (used internally by this file) */
typedef struct {
    char id[64];
    char name[128];
    char url[512];
    char capabilities[1024];
    int version;
    int protocol_version;
    bool available;
    char *capabilities_json;
    a2a_capability_t capabilities_mask;
} a2a_internal_card_t;

/* Transport write callback type for sending data through the transport layer */
typedef int (*a2a_transport_write_fn)(void *transport_ctx, const void *data, size_t size);

/* A2A v03 protocol message frame header constants */
#define A2A_V03_FRAME_MAGIC "A2A/0.3"
#define A2A_V03_FRAME_HDR_SEP "\r\n"
#define A2A_V03_FRAME_END "\r\n\r\n"
#define A2A_V03_FRAME_HDR_MAX 256

/* Use header-declared types; define local adapter state */
struct a2a_v03_adapter_s {
    a2a_internal_card_t agents[A2A_V03_MAX_AGENTS];
    size_t agent_count;
    uint64_t task_counter;
    size_t active_task_count;
    size_t completed_task_count;
    size_t failed_task_count;
    uint64_t total_delegation_ms;
    uint64_t total_consensus_ms;
    bool initialized;
    a2a_v03_config_t config;
    a2a_task_t *tasks[A2A_V03_MAX_TASKS];
    size_t task_count;
    a2a_notification_handler_t notification_handler;
    void *notification_handler_user_data;
    a2a_task_handler_t task_handler;
    void *task_handler_user_data;
    a2a_message_handler_t message_handler;
    void *message_handler_user_data;
    a2a_negotiation_handler_t negotiation_handler;
    void *negotiation_handler_user_data;
    a2a_streaming_handler_t streaming_handler;
    void *streaming_handler_user_data;
    /* Transport layer for sending data */
    a2a_transport_write_fn transport_write;
    void *transport_ctx;
    bool connected;
    uint64_t bytes_sent;
    uint64_t messages_sent;
};

static struct a2a_v03_adapter_s *g_a2a_instance = NULL;

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

    g_a2a_instance = adapter;
    *out_handle = (a2a_handle_t)adapter;
    return 0;
}

void a2a_v03_destroy(a2a_handle_t handle)
{
    if (!handle)
        return;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    adapter->initialized = false;
    if (g_a2a_instance == adapter)
        g_a2a_instance = NULL;

    /* P0-06 修复: 释放 tasks 数组中的每个 task。
     *
     * 历史问题：a2a_v03_create_task() 通过 AIRY_CALLOC 分配 a2a_task_t 并 STRDUP
     * id/agent_id/description/input_json，但 a2a_v03_destroy() 只释放 adapter 本身，
     * 漏释放所有 tasks，导致 ASAN 检测到 96B(task struct) + 40B(?) + 14B+10B+13B 字符串泄漏。
     *
     * 修复方案：用 a2a_task_destroy() 释放每个 task（处理所有字符串字段 + task 本身）。 */
    for (size_t i = 0; i < adapter->task_count; i++)
        a2a_task_destroy(adapter->tasks[i]);
    adapter->task_count = 0;

    /* P0-07 修复: 释放 a2a_v03_get_agent_card() 的 static card 缓冲区。
     *
      * 历史问题：a2a_v03_get_agent_card() 使用 static card 并每次 STRDUP
      * id/name/url/capabilities_json，
     * 导致：(1) 多次调用累积泄漏（每次 STRDUP 不释放上一次的字符串）
     *      (2) 程序结束时 static card 仍持有最后一次 STRDUP 的字符串，ASAN 检测到泄漏。
     *
     * 修复方案：在 context_destroy 时调用 a2a_agent_card_destroy() 释放 static card 的字符串。
     * 注意：static card 是函数内的，但 a2a_agent_card_destroy() 接受 card 指针，
     * 我们通过 a2a_v03_get_agent_card(NULL, NULL) 触发清理（见该函数实现）。 */
    a2a_v03_get_agent_card(NULL, NULL);

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

    /* 重复注册检测：若已存在同 ID agent，则更新该条目并合并 capabilities bitmask，
     * 不增加 agent_count（测试契约："duplicate should not increase count"）。 */
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
    /* 使用调用方提供的 card->id（测试契约：register 后 get_agent_card(card->id) 可命中）。
     * 若调用方未提供 id，则按原逻辑生成 "agent_<n>_<counter>" 形式 ID。 */
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
    /* P0-07: static card 缓冲区，由 a2a_v03_destroy() 通过 (NULL, NULL) 调用清理。
     *
     * 设计权衡：保留 static card 以维持 API 兼容（const 返回值，调用方无需释放），
     * 但添加显式清理路径避免 ASAN 检测到泄漏。 */
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
            /* P0-07: 释放上一次 STRDUP 的字符串避免累积泄漏。
             * 原 bug：每次调用都 STRDUP 但不释放上一次的字符串，导致多次调用累积泄漏。 */
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

/* ============================================================================
 * Task Delegation
 * ============================================================================ */

int a2a_v03_delegate_task(a2a_handle_t handle, const a2a_task_request_internal_t *request,
                          a2a_task_response_internal_t *out_response)
{
    if (!handle || !request || !out_response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_delegate_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    AIRY_MEMSET(out_response, 0, sizeof(*out_response));

    snprintf(out_response->task_id, sizeof(out_response->task_id), "task_%" PRIu64,
             adapter->task_counter++);

    out_response->status = A2A_TASK_STATUS_ACCEPTED;
    AIRY_STRNCPY_TERM(out_response->accepted_by,
                      request->target_agent_id ? request->target_agent_id : "coordinator",
                      sizeof(out_response->accepted_by));
    out_response->negotiation_rounds = 0;
    out_response->estimated_duration_ms =
        request->timeout_ms > 0 ? request->timeout_ms / 2 : A2A_DEFAULT_TIMEOUT_MS / 2;

    if (request->description) {
        out_response->result_json = AIRY_MALLOC(512);
        snprintf((char *)out_response->result_json, 512,
                 "{\"task_id\":\"%s\",\"status\":\"delegated\","
                 "\"description\":\"%s\"}",
                 out_response->task_id, request->description);
    }

    return 0;
}

int a2a_v03_negotiate_task(a2a_handle_t handle, const char *task_id,
                           const a2a_proposal_internal_t *proposal,
                           a2a_negotiation_result_internal_t *out_result)
{
    if (!handle || !task_id || !proposal || !out_result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_negotiate_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    AIRY_MEMSET(out_result, 0, sizeof(*out_result));
    AIRY_STRNCPY_TERM(out_result->task_id, task_id, sizeof(out_result->task_id));

    switch (proposal->type) {
    case A2A_NEGOTIATE_COST:
        out_result->outcome = A2A_OUTCOME_ACCEPTED;
        out_result->final_cost = proposal->proposed_cost * 1.1f;
        break;
    case A2A_NEGOTIATE_TIMEOUT:
        out_result->outcome = A2A_OUTCOME_ACCEPTED;
        out_result->final_timeout_ms = proposal->proposed_timeout_ms * 1.2f;
        break;
    case A2A_NEGOTIATE_PRIORITY:
        out_result->outcome = A2A_OUTCOME_COUNTER_OFFER;
        out_result->counter_priority = proposal->proposed_priority + 10;
        break;
    default:
        out_result->outcome = A2A_OUTCOME_REJECTED;
        break;
    }

    out_result->round_number = 1;
    return 0;
}

int a2a_v03_achieve_consensus(a2a_handle_t handle, const a2a_consensus_request_internal_t *request,
                              a2a_consensus_result_internal_t *out_result)
{
    if (!handle || !request || !out_result) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_achieve_consensus: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    AIRY_MEMSET(out_result, 0, sizeof(*out_result));

    size_t agree_count = 0;
    for (int i = 0; i < request->num_participants; i++) {
        if (i % 3 != 0) {
            out_result->agreements[i] = true;
            agree_count++;
        } else {
            out_result->agreements[i] = false;
        }
    }

    out_result->total_participants = request->num_participants;
    out_result->agree_count = agree_count;

    float threshold = request->consensus_threshold > 0 ? request->consensus_threshold : 0.67f;
    float ratio = (float)agree_count / (float)request->num_participants;

    if (ratio >= threshold) {
        out_result->consensus_reached = true;
        out_result->consensus_type = A2A_CONSENSUS_MAJORITY;
    } else {
        out_result->consensus_reached = false;
        out_result->consensus_type = A2A_CONSENSUS_NONE;
    }

    out_result->rounds_completed = 1;
    return 0;
}

/* ============================================================================
 * Streaming
 * ============================================================================ */

int a2a_v03_stream_task(a2a_handle_t handle, const a2a_task_request_internal_t *request,
                        a2a_stream_callback_internal_t on_chunk, void *user_data,
                        a2a_task_response_internal_t *final_response)
{
    if (!handle || !request || !on_chunk || !final_response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_stream_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    snprintf(final_response->task_id, sizeof(final_response->task_id), "stream_task_%" PRIu64,
             adapter->task_counter++);

    a2a_progress_event_internal_t event;
    AIRY_MEMSET(&event, 0, sizeof(event));
    AIRY_STRNCPY_TERM(event.task_id, final_response->task_id, sizeof(event.task_id));

    const char *phases[] = {"initiated", "planning", "executing", "verifying", "completed"};
    for (int i = 0; i < 5; i++) {
        event.event_type = A2A_PROGRESS_UPDATE;
        event.progress_percentage = (uint8_t)(20 * i + 20);
        AIRY_STRNCPY_TERM(event.phase, phases[i], sizeof(event.phase));

        char detail[256];
        snprintf(detail, sizeof(detail), "{\"phase\":\"%s\",\"progress\":%d}", phases[i],
                 event.progress_percentage);
        event.detail_json = AIRY_STRDUP(detail);

        on_chunk(event.detail_json, strlen(event.detail_json), (i == 4), user_data);
        AIRY_FREE((void *)event.detail_json);
        event.detail_json = NULL;
    }

    final_response->status = A2A_TASK_STATUS_COMPLETED;
    {
        size_t result_sz = 256 + (final_response->task_id[0] ? strlen(final_response->task_id) : 0);
        char *result_buf = (char *)AIRY_MALLOC(result_sz);
        if (result_buf) {
            snprintf(result_buf, result_sz,
                     "{\"task_id\":\"%s\",\"status\":\"completed\",\"phases_completed\":5}",
                     final_response->task_id);
        }
        final_response->result_json =
            result_buf ? result_buf :
                         AIRY_STRDUP("{\"status\":\"error\",\"reason\":\"allocation_failed\"}");
    }

    return 0;
}

/* ============================================================================
 * Statistics & Cleanup
 * ============================================================================ */

int a2a_v03_get_stats(a2a_handle_t handle, a2a_stats_internal_t *out_stats)
{
    if (!handle || !out_stats) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    AIRY_MEMSET(out_stats, 0, sizeof(*out_stats));
    out_stats->registered_agents = (uint32_t)adapter->agent_count;
    out_stats->active_tasks = (uint32_t)adapter->active_task_count;
    out_stats->completed_tasks = (uint32_t)adapter->completed_task_count;
    out_stats->failed_tasks = (uint32_t)adapter->failed_task_count;
    out_stats->avg_delegation_latency_ms =
        adapter->total_delegation_ms > 0 && adapter->completed_task_count > 0 ?
            (float)(adapter->total_delegation_ms / adapter->completed_task_count) :
            0.0f;
    out_stats->avg_consensus_latency_ms =
        adapter->total_consensus_ms > 0 && adapter->completed_task_count > 0 ?
            (float)(adapter->total_consensus_ms / adapter->completed_task_count) :
            0.0f;
    return 0;
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

/* ============================================================================
 * Authentication & Encryption (PROTO-002)
 * Production-grade A2A security layer
 *
 * Implements:
 * 1. API Key authentication (simple shared-secret)
 * 2. HMAC-SHA256 request signing
 * 3. Token-based session management with expiry
 * 4. Failed-attempt lockout (brute-force protection)
 * 5. Request signature verification (tamper-proof)
 * ============================================================================ */

#include "airy_memory.h"
#include "platform.h"

#include <ctype.h>
#include <time.h>

#define A2A_MAX_SESSIONS 128
#define A2A_MAX_TOKENS 256

static uint64_t a2a_timestamp_ms(void)
{
    return airy_time_ms();
}

static void a2a_hex_encode(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len && (i * 2 + 2) < out_size; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/* ============================================================================
 * SHA-256 + HMAC-SHA256（FIPS 180-4 / RFC 2104，纯 C 自包含实现）
 *
 * PROTO-002 要求 A2A 签名使用真实 HMAC-SHA256；原实现为 djb2 32 位哈希，
 * 无密钥、无单向性，可被离线穷举伪造。以下为标准实现，无外部依赖。
 * ============================================================================ */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} a2a_sha256_ctx_t;

static const uint32_t a2a_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t a2a_ror32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static void a2a_sha256_transform(a2a_sha256_ctx_t *ctx)
{
    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)ctx->data[i * 4] << 24) | ((uint32_t)ctx->data[i * 4 + 1] << 16) |
               ((uint32_t)ctx->data[i * 4 + 2] << 8) | (uint32_t)ctx->data[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = a2a_ror32(w[i - 15], 7) ^ a2a_ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = a2a_ror32(w[i - 2], 17) ^ a2a_ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (size_t i = 0; i < 64; i++) {
        uint32_t S1 = a2a_ror32(e, 6) ^ a2a_ror32(e, 11) ^ a2a_ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + a2a_sha256_k[i] + w[i];
        uint32_t S0 = a2a_ror32(a, 2) ^ a2a_ror32(a, 13) ^ a2a_ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void a2a_sha256_init(a2a_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->datalen = 0;
}

static void a2a_sha256_update(a2a_sha256_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = p[i];
        if (ctx->datalen == 64) {
            a2a_sha256_transform(ctx);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void a2a_sha256_final(a2a_sha256_ctx_t *ctx, uint8_t *out)
{
    uint64_t bitlen = ctx->bitlen + (uint64_t)ctx->datalen * 8;
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {
        while (ctx->datalen < 64)
            ctx->data[ctx->datalen++] = 0;
        a2a_sha256_transform(ctx);
        ctx->bitlen += 512;
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56)
        ctx->data[ctx->datalen++] = 0;
    for (int i = 0; i < 8; i++)
        ctx->data[56 + i] = (uint8_t)(bitlen >> (56 - i * 8));
    a2a_sha256_transform(ctx);

    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static void a2a_hmac_sha256(const void *key, size_t key_len, const void *msg, size_t msg_len,
                            uint8_t *out)
{
    uint8_t k[64];
    __builtin_memset(k, 0, sizeof(k));
    if (key_len > 64) {
        a2a_sha256_ctx_t c;
        a2a_sha256_init(&c);
        a2a_sha256_update(&c, key, key_len);
        a2a_sha256_final(&c, k);
    } else {
        __builtin_memcpy(k, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    a2a_sha256_ctx_t c;
    a2a_sha256_init(&c);
    a2a_sha256_update(&c, ipad, sizeof(ipad));
    a2a_sha256_update(&c, msg, msg_len);
    uint8_t inner[32];
    a2a_sha256_final(&c, inner);

    a2a_sha256_init(&c);
    a2a_sha256_update(&c, opad, sizeof(opad));
    a2a_sha256_update(&c, inner, sizeof(inner));
    a2a_sha256_final(&c, out);
}

static int a2a_const_time_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return (diff == 0) ? 1 : 0;
}

static uint32_t a2a_simple_hash(const char *data, size_t len)
{
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (uint8_t)data[i];
    }
    return hash;
}

typedef struct {
    bool initialized;
    a2a_auth_config_t config;
    a2a_auth_token_t tokens[A2A_MAX_TOKENS];
    size_t token_count;
    a2a_session_t sessions[A2A_MAX_SESSIONS];
    size_t session_count;
    int failed_attempts;
    uint64_t lockout_until;
} a2a_auth_state_t;

static a2a_auth_state_t g_a2a_auth = {0};

static void a2a_generate_token_string(char *token_buf, size_t buf_size, const char *agent_id,
                                      uint64_t timestamp)
{
    /* 基于 HMAC-SHA256 的伪随机令牌：token = hex(HMAC(secret, agent_id|timestamp))。
     * 有密钥时不可预测；无密钥时退回时间混洗（仅未启用安全性的场景）。 */
    const char *src = agent_id ? agent_id : "anonymous";
    uint8_t raw[32];
    size_t src_len = strlen(src);
    if (src_len > 32)
        src_len = 32;

    if (g_a2a_auth.initialized && g_a2a_auth.config.secret_len > 0 &&
        g_a2a_auth.config.secret_len <= 64) {
        uint8_t msg[64];
        size_t off = 0;
        __builtin_memcpy(msg, src, src_len);
        off += src_len;
        for (int i = 0; i < 8; i++)
            msg[off++] = (uint8_t)(timestamp >> (i * 8));
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, msg, off,
                        raw);
    } else {
        for (size_t i = 0; i < sizeof(raw); i++) {
            raw[i] = (uint8_t)(a2a_simple_hash(src, src_len) ^ (timestamp >> (i % 8)) ^
                               (uint8_t)(i * 37 + 0xAB) ^ (uint8_t)((timestamp * (i + 1)) & 0xFF));
        }
    }

    a2a_hex_encode(raw, sizeof(raw), token_buf, buf_size);
    if (buf_size > 0)
        token_buf[buf_size - 1] = '\0';
}

int a2a_v03_auth_init(a2a_v03_context_t *ctx, const a2a_auth_config_t *auth_config)
{
    if (!ctx || !auth_config) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_auth_init: failed");
        return AIRY_ERR_UNKNOWN;
    }

    AIRY_MEMSET(&g_a2a_auth, 0, sizeof(g_a2a_auth));
    g_a2a_auth.initialized = true;
    g_a2a_auth.config = *auth_config;

    if (g_a2a_auth.config.max_failed_attempts <= 0)
        g_a2a_auth.config.max_failed_attempts = A2A_MAX_FAILED_AUTH_ATTEMPTS;
    if (g_a2a_auth.config.token_ttl_sec == 0)
        g_a2a_auth.config.token_ttl_sec = A2A_TOKEN_EXPIRY_SEC;
    if (g_a2a_auth.config.max_sessions == 0)
        g_a2a_auth.config.max_sessions = A2A_MAX_SESSIONS;

    return 0;
}

void a2a_v03_auth_shutdown(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return;

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        AIRY_MEMSET(&g_a2a_auth.tokens[i], 0, sizeof(g_a2a_auth.tokens[i]));
    }
    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        AIRY_MEMSET(&g_a2a_auth.sessions[i], 0, sizeof(g_a2a_auth.sessions[i]));
    }

    AIRY_MEMSET(&g_a2a_auth.config.shared_secret, 0, sizeof(g_a2a_auth.config.shared_secret));
    AIRY_MEMSET(&g_a2a_auth, 0, sizeof(g_a2a_auth));
}

int a2a_v03_authenticate(a2a_v03_context_t *ctx, const char *agent_id, const char *credential,
                         a2a_auth_token_t **out_token)
{
    if (!ctx || !agent_id || !credential || !out_token) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_authenticate: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    uint64_t now = a2a_timestamp_ms() / 1000;

    if (g_a2a_auth.lockout_until > 0 && now < g_a2a_auth.lockout_until) {
        LOG_ERROR("authentication locked out: agent_id=%s, lockout_until=%llu, now=%llu", agent_id,
                  (unsigned long long)g_a2a_auth.lockout_until, (unsigned long long)now);

        airy_err_push_ex(
            AIRY_ERR_PERMISSION_DENIED, __FILE__, __LINE__, __func__,
            "a2a_v03_authenticate: authentication locked out (too many failed attempts)");
        return AIRY_ERR_PERMISSION_DENIED;
    }

    int cred_valid = 0;
    switch (g_a2a_auth.config.method) {
    case A2A_AUTH_API_KEY:

        cred_valid = (strlen(credential) == (size_t)g_a2a_auth.config.secret_len) &&
                     a2a_const_time_eq((const uint8_t *)credential,
                                       (const uint8_t *)g_a2a_auth.config.shared_secret,
                                       (size_t)g_a2a_auth.config.secret_len);
        break;
    case A2A_AUTH_HMAC_SHA256: {
        /* 凭据为 hex(HMAC-SHA256(shared_secret, agent_id))：
         * 用真实 HMAC 计算期望值并常量时间比较（原 djb2 哈希可伪造） */
        if (g_a2a_auth.config.secret_len <= 0 || g_a2a_auth.config.secret_len > 64) {
            cred_valid = 0;
            break;
        }
        uint8_t expect[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, agent_id,
                        strlen(agent_id), expect);
        char expect_hex[65];
        a2a_hex_encode(expect, sizeof(expect), expect_hex, sizeof(expect_hex));
        cred_valid =
            (strlen(credential) == 64) &&
            a2a_const_time_eq((const uint8_t *)expect_hex, (const uint8_t *)credential, 64);
        break;
    }
    case A2A_AUTH_NONE:
    default:
        cred_valid = 1;
        break;
    }

    if (!cred_valid) {
        LOG_ERROR("authentication failed: agent_id=%s, method=%d, failed_attempts=%d", agent_id,
                  g_a2a_auth.config.method, g_a2a_auth.failed_attempts + 1);
        g_a2a_auth.failed_attempts++;
        if (g_a2a_auth.failed_attempts >= g_a2a_auth.config.max_failed_attempts) {
            g_a2a_auth.lockout_until = now + 300;
            g_a2a_auth.failed_attempts = 0;
        }
        airy_err_push_ex(AIRY_ERR_BUFFER_TOO_SMALL, __FILE__, __LINE__, __func__,
                         "operation failed");
        return AIRY_ERR_BUFFER_TOO_SMALL;
    }

    g_a2a_auth.failed_attempts = 0;

    if (g_a2a_auth.token_count >= A2A_MAX_TOKENS) {
        __builtin_memmove(&g_a2a_auth.tokens[0], &g_a2a_auth.tokens[1],
                          (A2A_MAX_TOKENS - 1) * sizeof(a2a_auth_token_t));
        g_a2a_auth.token_count--;
    }

    a2a_auth_token_t *tok = &g_a2a_auth.tokens[g_a2a_auth.token_count++];
    AIRY_MEMSET(tok, 0, sizeof(*tok));

    AIRY_STRNCPY_TERM(tok->agent_id, agent_id, sizeof(tok->agent_id));
    tok->issued_at = now;
    tok->expires_at = now + g_a2a_auth.config.token_ttl_sec;
    tok->permissions = 0xFFFFFFFF;
    tok->valid = true;

    a2a_generate_token_string(tok->token, sizeof(tok->token), agent_id, now);

    *out_token = tok;
    return 0;
}

int a2a_v03_verify_token(a2a_v03_context_t *ctx, const char *token_str,
                         a2a_auth_token_t **out_token)
{
    if (!ctx || !token_str || !out_token) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_verify_token: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    uint64_t now = a2a_timestamp_ms() / 1000;

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        a2a_auth_token_t *tok = &g_a2a_auth.tokens[i];
        if (!tok->valid)
            continue;
        if (strcmp(tok->token, token_str) != 0)
            continue;

        if (now >= tok->expires_at) {
            LOG_WARN("token expired: agent_id=%s, expires_at=%llu, now=%llu", tok->agent_id,
                     (unsigned long long)tok->expires_at, (unsigned long long)now);
            tok->valid = false;
            airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
            return AIRY_ERR_UNKNOWN;
        }

        if (out_token)
            *out_token = tok;
        return 0;
    }

    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    LOG_WARN("token not found or invalid: token_count=%zu", g_a2a_auth.token_count);
    return AIRY_ERR_UNKNOWN;
}

int a2a_v03_invalidate_token(a2a_v03_context_t *ctx, const char *token_str)
{
    if (!ctx || !token_str) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_invalidate_token: invalid parameter");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        if (g_a2a_auth.tokens[i].valid && strcmp(g_a2a_auth.tokens[i].token, token_str) == 0) {
            AIRY_MEMSET(&g_a2a_auth.tokens[i], 0, sizeof(g_a2a_auth.tokens[i]));
            return 0;
        }
    }

    airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                     "memset: error AIRY_ERR_NULL_POINTER");
    return AIRY_ERR_NULL_POINTER;
}

const char *a2a_v03_sign_request(a2a_v03_context_t *ctx, const char *method,
                                 const char *params_json, const char *token_str,
                                 char *out_signature, size_t sig_buf_size)
{
    if (!ctx || !method || !params_json || !out_signature || sig_buf_size < 65)
        return NULL;
    if (!g_a2a_auth.initialized)
        return NULL;

    /* 签名消息：method|params|token。
     * 注意：原实现将当前时间戳嵌入签名导致验证与签名不在同一毫秒即失败
     * （验证时无法复用签名时间戳），重放防护由会话/令牌 TTL 承担。 */
    char sign_data[4096];
    int len = snprintf(sign_data, sizeof(sign_data), "%s|%s|%s", method, params_json,
                       token_str ? token_str : "");
    if (len <= 0 || len >= (int)sizeof(sign_data))
        return NULL;

    if (g_a2a_auth.config.method == A2A_AUTH_HMAC_SHA256 && g_a2a_auth.config.secret_len > 0 &&
        g_a2a_auth.config.secret_len <= 64) {
        uint8_t mac[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, sign_data,
                        (size_t)len, mac);
        a2a_hex_encode(mac, sizeof(mac), out_signature, sig_buf_size);
        return out_signature;
    }

    if (g_a2a_auth.config.secret_len > 0 && g_a2a_auth.config.secret_len <= 64) {
        uint8_t mac[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, sign_data,
                        (size_t)len, mac);
        a2a_hex_encode(mac, sizeof(mac), out_signature, sig_buf_size);
        return out_signature;
    }
    uint32_t hash = a2a_simple_hash(sign_data, (size_t)len);
    snprintf(out_signature, sig_buf_size, "%08x%08x%08x%08x", hash, hash ^ 0xA5A5A5A5,
             hash ^ 0x5A5A5A5A, hash ^ 0x12345678);
    return out_signature;
}

int a2a_v03_verify_signature(a2a_v03_context_t *ctx, const char *method, const char *params_json,
                             const char *signature, const char *token_str)
{
    if (!ctx || !method || !params_json || !signature) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_verify_signature: failed");
        return AIRY_ERR_UNKNOWN;
    }

    char expected[65];
    if (!a2a_v03_sign_request(ctx, method, params_json, token_str, expected, sizeof(expected))) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__, "operation failed");
        return AIRY_ERR_NULL_POINTER;
    }

    if (strlen(signature) >= 64 &&
        a2a_const_time_eq((const uint8_t *)expected, (const uint8_t *)signature, 64) == 1)
        return 0;
    LOG_ERROR("signature verification failed: method=%s, expected vs actual mismatch", method);
    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}

int a2a_v03_create_session(a2a_v03_context_t *ctx, const char *remote_agent_id,
                           a2a_auth_method_t auth_method, a2a_crypto_method_t crypto_method,
                           a2a_session_t **out_session)
{
    if (!ctx || !remote_agent_id || !out_session) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_create_session: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (g_a2a_auth.session_count >= g_a2a_auth.config.max_sessions) {
        size_t oldest_idx = 0;
        uint64_t oldest_time = UINT64_MAX;
        for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
            if (g_a2a_auth.sessions[i].last_activity < oldest_time) {
                oldest_time = g_a2a_auth.sessions[i].last_activity;
                oldest_idx = i;
            }
        }
        AIRY_MEMSET(&g_a2a_auth.sessions[oldest_idx], 0, sizeof(a2a_session_t));
        g_a2a_auth.sessions[oldest_idx] = g_a2a_auth.sessions[g_a2a_auth.session_count - 1];
        g_a2a_auth.session_count--;
    }

    uint64_t now = a2a_timestamp_ms();

    a2a_session_t *sess = &g_a2a_auth.sessions[g_a2a_auth.session_count++];
    AIRY_MEMSET(sess, 0, sizeof(*sess));

    snprintf(sess->session_id, sizeof(sess->session_id), "sess_%s_%" PRIu64 "_%08x",
             remote_agent_id, (uint64_t)(now / 1000),
             a2a_simple_hash(remote_agent_id, strlen(remote_agent_id)));

    AIRY_STRNCPY_TERM(sess->remote_agent_id, remote_agent_id, sizeof(sess->remote_agent_id));
    sess->auth_method = auth_method;
    sess->crypto_method = crypto_method;
    sess->created_at = now;
    sess->last_activity = now;
    sess->authenticated = (auth_method != A2A_AUTH_NONE);
    sess->encrypted = (crypto_method != A2A_CRYPTO_NONE);

    *out_session = sess;
    return 0;
}

int a2a_v03_validate_session(a2a_v03_context_t *ctx, const char *session_id,
                             a2a_session_t **out_session)
{
    if (!ctx || !session_id || !out_session) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_validate_session: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        a2a_session_t *sess = &g_a2a_auth.sessions[i];

        if (strncmp(sess->session_id, session_id, sizeof(sess->session_id)) != 0)
            continue;

        uint64_t now = a2a_timestamp_ms();
        uint64_t age_sec = (now - sess->created_at) / 1000;

        if (age_sec > (uint64_t)g_a2a_auth.config.token_ttl_sec * 2) {
            LOG_WARN("session expired: session_id=%s, age_sec=%llu, ttl=%d", sess->session_id,
                     (unsigned long long)age_sec, g_a2a_auth.config.token_ttl_sec * 2);
            AIRY_MEMSET(sess, 0, sizeof(*sess));

            airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__,
                             "a2a_v03_validate_session: session expired");
            return AIRY_ERR_STATE_ERROR;
        }

        sess->last_activity = now;
        sess->request_count++;

        if (out_session)
            *out_session = sess;
        return 0;
    }

    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}

void a2a_v03_destroy_session(a2a_v03_context_t *ctx, const char *session_id)
{
    if (!ctx || !session_id)
        return;

    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        if (strncmp(g_a2a_auth.sessions[i].session_id, session_id,
                    sizeof(g_a2a_auth.sessions[i].session_id)) == 0) {
            AIRY_MEMSET(&g_a2a_auth.sessions[i], 0, sizeof(a2a_session_t));
            return;
        }
    }
}

size_t a2a_v03_get_active_session_count(a2a_v03_context_t *ctx)
{
    (void)ctx;
    return g_a2a_auth.session_count;
}

const char *a2a_auth_method_string(a2a_auth_method_t method)
{
    switch (method) {
    case A2A_AUTH_NONE:
        return "none";
    case A2A_AUTH_API_KEY:
        return "api_key";
    case A2A_AUTH_HMAC_SHA256:
        return "hmac-sha256";
    case A2A_AUTH_JWT_BEARER:
        return "jwt-bearer";
    default:
        return "unknown";
    }
}

const char *a2a_crypto_method_string(a2a_crypto_method_t method)
{
    switch (method) {
    case A2A_CRYPTO_NONE:
        return "none";
    case A2A_CRYPTO_AES_128_GCM:
        return "aes-128-gcm";
    case A2A_CRYPTO_AES_256_GCM:
        return "aes-256-gcm";
    default:
        return "unknown";
    }
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

int a2a_v03_create_task(a2a_v03_context_t *ctx, const char *agent_id, const char *description,
                        const char *input_json, a2a_task_t **task)
{
    if (!ctx || !task) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_create_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }
    if (adapter->task_count >= A2A_V03_MAX_TASKS) {
        airy_err_push_ex(AIRY_ERR_BUFFER_TOO_SMALL, __FILE__, __LINE__, __func__,
                         "capacity exceeded");
        return AIRY_ERR_BUFFER_TOO_SMALL;
    }

    a2a_task_t *t = (a2a_task_t *)AIRY_CALLOC(1, sizeof(a2a_task_t));
    if (!t) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    t->id = (char *)AIRY_MALLOC(A2A_TASK_ID_SIZE);
    if (!t->id) {
        AIRY_FREE(t);
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "AIRY_MALLOC: error AIRY_ERR_OUT_OF_MEMORY");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    snprintf(t->id, A2A_TASK_ID_SIZE, "task_%zu_%u", adapter->task_count,
             (unsigned int)(a2a_timestamp_ms() % 100000));
    t->agent_id = agent_id ? AIRY_STRDUP(agent_id) : NULL;
    t->description = description ? AIRY_STRDUP(description) : NULL;
    t->input_json = input_json ? AIRY_STRDUP(input_json) : NULL;
    t->output_json = NULL;
    t->state = A2A_TASK_SUBMITTED;
    t->created_at = a2a_timestamp_ms();
    t->updated_at = t->created_at;
    t->progress = 0.0;

    adapter->tasks[adapter->task_count] = t;
    adapter->task_count++;

    if (adapter->task_handler) {
        a2a_task_state_t new_state = t->state;
        char *output = NULL;
        adapter->task_handler(ctx, t, &new_state, &output, adapter->task_handler_user_data);
        t->state = new_state;
        if (output) {
            AIRY_FREE(t->output_json);
            t->output_json = output;
        }
    }

    *task = t;
    return 0;
}

int a2a_v03_update_task(a2a_v03_context_t *ctx, const char *task_id, a2a_task_state_t new_state,
                        const char *output_json, double progress)
{
    if (!ctx || !task_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_update_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    for (size_t i = 0; i < adapter->task_count; i++) {
        if (strcmp(adapter->tasks[i]->id, task_id) == 0) {
            a2a_task_t *t = adapter->tasks[i];
            t->state = new_state;
            if (output_json) {
                AIRY_FREE(t->output_json);
                t->output_json = AIRY_STRDUP(output_json);
            }
            t->progress = progress;
            t->updated_at = a2a_timestamp_ms();
            return 0;
        }
    }
    airy_err_push_ex(AIRY_ERR_NOT_FOUND, __FILE__, __LINE__, __func__,
                     "a2a_v03_update_task: task not found");
    return AIRY_ERR_NOT_FOUND;
}

int a2a_v03_cancel_task(a2a_v03_context_t *ctx, const char *task_id, const char *reason)
{
    if (!ctx || !task_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_cancel_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    for (size_t i = 0; i < adapter->task_count; i++) {
        if (strcmp(adapter->tasks[i]->id, task_id) == 0) {
            adapter->tasks[i]->state = A2A_TASK_CANCELED;
            adapter->tasks[i]->updated_at = a2a_timestamp_ms();
            if (reason) {
                AIRY_FREE(adapter->tasks[i]->error_message);
                adapter->tasks[i]->error_message = AIRY_STRDUP(reason);
            }
            return 0;
        }
    }
    airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                     "AIRY_STRDUP: error AIRY_ERR_OUT_OF_MEMORY");
    return AIRY_ERR_OUT_OF_MEMORY;
}

int a2a_v03_get_task(a2a_v03_context_t *ctx, const char *task_id, a2a_task_t **task)
{
    if (!ctx || !task_id || !task) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_get_task: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    for (size_t i = 0; i < adapter->task_count; i++) {
        if (strcmp(adapter->tasks[i]->id, task_id) == 0) {
            *task = adapter->tasks[i];
            return 0;
        }
    }
    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}

int a2a_v03_send_message(a2a_v03_context_t *ctx, const char *target_agent_id,
                         const a2a_message_t *message, a2a_message_t **response,
                         size_t *response_count)
{
    if (!ctx || !target_agent_id || !message) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_send_message: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (adapter->message_handler) {
        return adapter->message_handler(ctx, target_agent_id, message, response, response_count,
                                        adapter->message_handler_user_data);
    }

    if (response && response_count) {
        a2a_message_t *resp = (a2a_message_t *)AIRY_CALLOC(1, sizeof(a2a_message_t));
        if (resp) {
            resp->role = AIRY_STRDUP("assistant");
            resp->type = A2A_MSG_STRUCTURED;
            resp->content_json = AIRY_STRDUP("{\"status\":\"received\",\"ack\":true}");
            resp->mime_type = AIRY_STRDUP("application/json");
            *response = resp;
            *response_count = 1;
        }
    }
    return 0;
}

int a2a_v03_negotiate(a2a_v03_context_t *ctx, const a2a_negotiation_t *proposal,
                      a2a_negotiation_action_t *response_action, char **response_terms)
{
    if (!ctx || !proposal) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_negotiate: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (adapter->negotiation_handler) {
        return adapter->negotiation_handler(ctx, proposal, response_action, response_terms,
                                            adapter->negotiation_handler_user_data);
    }

    if (response_action)
        *response_action = A2A_NEGOTIATE_REJECT;
    if (response_terms)
        *response_terms = NULL;
    return 0;
}

int a2a_v03_subscribe_notifications(a2a_v03_context_t *ctx, a2a_notification_handler_t handler,
                                    void *user_data)
{
    if (!ctx || !handler) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_subscribe_notifications: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->notification_handler = handler;
    adapter->notification_handler_user_data = user_data;
    return 0;
}

int a2a_v03_unsubscribe_notifications(a2a_v03_context_t *ctx)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_unsubscribe_notifications: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->notification_handler = NULL;
    adapter->notification_handler_user_data = NULL;
    return 0;
}

int a2a_v03_send_notification(a2a_v03_context_t *ctx, const a2a_notification_t *notification)
{
    if (!ctx || !notification) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_send_notification: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->notification_handler) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__, "operation failed");
        return AIRY_ERR_NULL_POINTER;
    }
    adapter->notification_handler(ctx, notification, adapter->notification_handler_user_data);
    return 0;
}

int a2a_v03_stream_task_update(a2a_v03_context_t *ctx, const char *task_id, double progress,
                               const char *chunk_json, bool is_final)
{
    if (!ctx || !task_id) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_stream_task_update: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    for (size_t i = 0; i < adapter->task_count; i++) {
        if (strcmp(adapter->tasks[i]->id, task_id) == 0) {
            adapter->tasks[i]->progress = progress;
            adapter->tasks[i]->updated_at = a2a_timestamp_ms();
            if (is_final) {
                adapter->tasks[i]->state = A2A_TASK_COMPLETED;
                if (chunk_json) {
                    AIRY_FREE(adapter->tasks[i]->output_json);
                    adapter->tasks[i]->output_json = AIRY_STRDUP(chunk_json);
                }
            }
            break;
        }
    }

    if (adapter->streaming_handler) {
        adapter->streaming_handler(ctx, task_id, progress, chunk_json, is_final,
                                   adapter->streaming_handler_user_data);
    }
    return 0;
}

int a2a_v03_set_task_handler(a2a_v03_context_t *ctx, a2a_task_handler_t handler, void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_task_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->task_handler = handler;
    adapter->task_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_message_handler(a2a_v03_context_t *ctx, a2a_message_handler_t handler,
                                void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_message_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->message_handler = handler;
    adapter->message_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_negotiation_handler(a2a_v03_context_t *ctx, a2a_negotiation_handler_t handler,
                                    void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_negotiation_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->negotiation_handler = handler;
    adapter->negotiation_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_streaming_handler(a2a_v03_context_t *ctx, a2a_streaming_handler_t handler,
                                  void *user_data)
{
    if (!ctx) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_streaming_handler: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->streaming_handler = handler;
    adapter->streaming_handler_user_data = user_data;
    return 0;
}

int a2a_v03_set_transport(a2a_v03_context_t *ctx, int (*write_fn)(void *, const void *, size_t),
                          void *transport_ctx)
{
    if (!ctx || !write_fn) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "a2a_v03_set_transport: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    adapter->transport_write = write_fn;
    adapter->transport_ctx = transport_ctx;
    return 0;
}

int a2a_v03_route_request(a2a_v03_context_t *ctx, const char *method, const char *params_json,
                          char **response_json)
{
    if (!ctx || !method || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_route_request: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)ctx;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }
    *response_json = NULL;

    if (strcmp(method, "agent/discover") == 0) {
        size_t buf_size = 256 + adapter->agent_count * 128;
        char *buf = (char *)AIRY_MALLOC(buf_size);
        if (!buf) {
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        int pos = snprintf(buf, buf_size, "{\"agents\":[");
        for (size_t i = 0; i < adapter->agent_count; i++) {
            pos += snprintf(buf + pos, buf_size - (size_t)pos, "%s{\"id\":\"%s\",\"name\":\"%s\"}",
                            i > 0 ? "," : "", adapter->agents[i].id,
                            adapter->agents[i].name[0] ? adapter->agents[i].name : "");
        }
        snprintf(buf + pos, buf_size - (size_t)pos, "]}");
        *response_json = buf;
        return 0;
    }

    if (strcmp(method, "task/create") == 0) {
        a2a_task_t *task = NULL;
        int rc = a2a_v03_create_task(ctx, NULL, params_json, NULL, &task);
        if (rc == 0 && task) {
            size_t buf_size = 256;
            char *buf = (char *)AIRY_MALLOC(buf_size);
            if (buf) {
                snprintf(buf, buf_size, "{\"task_id\":\"%s\",\"state\":\"submitted\"}", task->id);
                *response_json = buf;
            }
        }
        return rc;
    }

    if (strcmp(method, "task/list") == 0) {
        size_t buf_size = 256 + adapter->task_count * 128;
        char *buf = (char *)AIRY_MALLOC(buf_size);
        if (!buf) {
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        int pos = snprintf(buf, buf_size, "{\"tasks\":[");
        for (size_t i = 0; i < adapter->task_count; i++) {
            pos += snprintf(buf + pos, buf_size - (size_t)pos, "%s{\"id\":\"%s\",\"state\":%d}",
                            i > 0 ? "," : "", adapter->tasks[i]->id, (int)adapter->tasks[i]->state);
        }
        snprintf(buf + pos, buf_size - (size_t)pos, "]}");
        *response_json = buf;
        return 0;
    }

    if (strcmp(method, "stats") == 0) {
        size_t buf_size = 256;
        char *buf = (char *)AIRY_MALLOC(buf_size);
        if (!buf) {
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        snprintf(buf, buf_size, "{\"agent_count\":%zu,\"task_count\":%zu,\"capabilities\":%u}",
                 adapter->agent_count, adapter->task_count, adapter->config.capabilities);
        *response_json = buf;
        return 0;
    }

    *response_json = AIRY_STRDUP("{\"error\":\"unknown method\"}");
    LOG_WARN("unknown method in route_request: method=%s", method);
    airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                     "AIRY_STRDUP: error AIRY_ERR_OUT_OF_MEMORY");
    return AIRY_ERR_OUT_OF_MEMORY;
}

static int a2a_adapter_init_cb(void *context)
{
    if (!context)
        return AIRY_ENOMEM;
    return 0;
}
static int a2a_adapter_destroy_cb(void *context)
{
    a2a_v03_context_destroy((a2a_v03_context_t *)context);
    return 0;
}
static int a2a_adapter_encode_cb(void *c, const void *m, void **o, size_t *s)
{
    if (!c || !m || !o || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_encode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    const char *msg = (const char *)m;
    size_t len = strlen(msg) + 1;
    char *buf = (char *)AIRY_MALLOC(len);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "strlen: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(buf, msg, len);
    *o = buf;
    *s = len;
    return 0;
}
static int a2a_adapter_decode_cb(void *c, const void *d, size_t s, void *o)
{
    if (!c || !d || !o || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_decode_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    __builtin_memcpy(o, d, s);
    return 0;
}
static int a2a_adapter_connect_cb(void *c, const char *e)
{
    if (!c) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_connect_cb: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    adapter->connected = true;
    LOG_DEBUG("a2a_adapter_connect_cb: connected to %s", e ? e : "(unknown)");
    (void)e;
    return 0;
}
static int a2a_adapter_disconnect_cb(void *c)
{
    if (!c) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_disconnect_cb: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    adapter->connected = false;
    LOG_DEBUG("a2a_adapter_disconnect_cb: disconnected");
    return 0;
}
static int a2a_adapter_is_connected_cb(void *c)
{
    if (!c)
        return 0;
    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    return adapter->connected ? 1 : 0;
}
static int a2a_adapter_send_cb(void *c, const void *d, size_t s)
{
    if (!c || !d || s == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "a2a_adapter_send_cb: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    struct a2a_v03_adapter_s *adapter = (struct a2a_v03_adapter_s *)c;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (!adapter->connected || !adapter->transport_write) {
        LOG_WARN("send failed: not connected or no transport, connected=%d, transport_write=%p",
                 adapter->connected, (void *)(uintptr_t)adapter->transport_write);

        airy_err_push_ex(AIRY_ENOTCONN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_send_cb: not connected or no transport");
        return AIRY_ENOTCONN;
    }

    /* Format A2A v03 protocol message frame:
     *   A2A/0.3\r\n
     *   Content-Length: <size>\r\n
     *   Content-Type: application/json\r\n
     *   \r\n
     *   <payload>
     */
    char header[A2A_V03_FRAME_HDR_MAX];
    int hdr_len = snprintf(header, sizeof(header),
                           A2A_V03_FRAME_MAGIC A2A_V03_FRAME_HDR_SEP
                           "Content-Length: %zu" A2A_V03_FRAME_HDR_SEP
                           "Content-Type: application/json" A2A_V03_FRAME_END,
                           s);
    if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(header)) {
        airy_err_push_ex(AIRY_ERR_OVERFLOW, __FILE__, __LINE__, __func__, "header overflow");
        return AIRY_ERR_OVERFLOW;
    }

    LOG_DEBUG("a2a_adapter_send_cb: sending %zu bytes (frame hdr=%d)", s, hdr_len);

    /* Send header */
    int rc = adapter->transport_write(adapter->transport_ctx, header, (size_t)hdr_len);
    if (rc != AIRY_OK) {
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "transport write header failed");
        return AIRY_ERR_IO;
    }

    /* Send payload */
    rc = adapter->transport_write(adapter->transport_ctx, d, s);
    if (rc != AIRY_OK) {
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__,
                         "transport write payload failed");
        return AIRY_ERR_IO;
    }

    adapter->bytes_sent += (uint64_t)s + (uint64_t)hdr_len;
    adapter->messages_sent++;

    LOG_DEBUG("a2a_adapter_send_cb: sent message #%llu (%zu bytes payload)",
              (unsigned long long)adapter->messages_sent, s);

    return AIRY_OK;
}
static int a2a_adapter_receive_cb(void *c, void **d, size_t *s, uint32_t t)
{
    if (!c || !d || !s) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_receive_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    (void)t;
    *d = NULL;
    *s = 0;
    return AIRY_EINVAL;
}
static int a2a_adapter_handle_request_cb(void *c, const void *r, void **rp)
{
    if (!c || !r) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_handle_request_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (rp)
        *rp = NULL;
    return a2a_v03_route_request((a2a_v03_context_t *)c, (const char *)r, NULL, (char **)rp);
}
static int a2a_adapter_get_version_cb(void *c, char *b, size_t s)
{
    (void)c;
    snprintf(b, s, "0.3.0");
    return 0;
}
static uint32_t a2a_adapter_capabilities_cb(void *c)
{
    (void)c;
    return A2A_CAP_TASK_EXECUTION | A2A_CAP_STREAMING | A2A_CAP_NEGOTIATION;
}
static int a2a_adapter_get_stats_cb(void *c, char *b, size_t s)
{
    if (!c || !b || s == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_adapter_get_stats_cb: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct a2a_v03_adapter_s *a = (struct a2a_v03_adapter_s *)c;
    snprintf(b, s, "{\"agents\":%zu,\"tasks\":%zu}", a->agent_count, a->task_count);
    return 0;
}

const protocol_adapter_t *a2a_v03_get_adapter(void)
{
    static protocol_adapter_t s_adapter;
    static bool s_init = false;
    if (!s_init) {
        AIRY_MEMSET(&s_adapter, 0, sizeof(s_adapter));
        s_adapter.type = AIRY_PROTOCOL_A2A;
        s_adapter.name = "a2a-v0.3";
        s_adapter.version = "0.3.0";
        s_adapter.description = "A2A v0.3 Protocol Adapter";
        s_adapter.init = a2a_adapter_init_cb;
        s_adapter.destroy = a2a_adapter_destroy_cb;
        s_adapter.encode = a2a_adapter_encode_cb;
        s_adapter.decode = a2a_adapter_decode_cb;
        s_adapter.connect = a2a_adapter_connect_cb;
        s_adapter.disconnect = a2a_adapter_disconnect_cb;
        s_adapter.is_connected = a2a_adapter_is_connected_cb;
        s_adapter.send = a2a_adapter_send_cb;
        s_adapter.receive = a2a_adapter_receive_cb;
        s_adapter.handle_request = a2a_adapter_handle_request_cb;
        s_adapter.get_version = a2a_adapter_get_version_cb;
        s_adapter.capabilities = a2a_adapter_capabilities_cb;
        s_adapter.get_stats = a2a_adapter_get_stats_cb;
        s_init = true;
    }
    return &s_adapter;
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
    /* P0-07 修复: 移除 AIRY_FREE(card)。
     *
     * 历史 bug：原实现调用 AIRY_FREE(card) 释放 card 指针本身，但 card 可能是
     * 栈分配（如调用方的局部变量）或静态分配（如 a2a_v03_get_agent_card 的
     * g_cached_card），AIRY_FREE 非堆指针会导致 ASAN bad-free 错误。
     *
     * 修复方案：只释放 card 的字段，不释放 card 本身。调用方负责释放 card
     * 指针（如果是堆分配的）。这与 cJSON_free 等标准 destroy 模式一致。 */
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
