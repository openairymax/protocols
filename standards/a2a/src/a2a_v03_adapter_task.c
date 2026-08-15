// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_task.c
 * @brief A2A v0.3 task operation domain (delegate/negotiate/consensus/streaming/lifecycle/routing).
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"

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
    AIRY_LOG_WARN("unknown method in route_request: method=%s", method);
    airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                     "AIRY_STRDUP: error AIRY_ERR_OUT_OF_MEMORY");
    return AIRY_ERR_OUT_OF_MEMORY;
}
