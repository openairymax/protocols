// SPDX-FileCopyrightText: 2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
// @owner: team-B
/**
 * @file agntcy_acp_adapter.c
 * @brief AGNTCY Agent Communication Protocol Adapter Implementation
 */

#include "agntcy_acp_adapter.h"

#include "airy_memory.h"
#include "platform.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.h"

static struct {
    agntcy_handle_t handle;
    bool proto_initialized;
    bool is_connected;
    char *connected_endpoint;
    void *send_buffer;
    size_t send_buffer_size;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} g_agntcy_state = {0};

int agntcy_acp_create(agntcy_handle_t **handle)
{
    if (!handle)
        return AIRY_ERR_NULL_POINTER;

    agntcy_handle_t *h = (agntcy_handle_t *)AIRY_CALLOC(1, sizeof(agntcy_handle_t));
    if (!h)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    h->initialized = true;
    *handle = h;
    return 0;
}

void agntcy_acp_destroy(agntcy_handle_t *handle)
{
    if (!handle)
        return;
    for (size_t i = 0; i < handle->task_count; i++) {
        if (handle->tasks[i]) {
            AIRY_FREE(handle->tasks[i]->workflow_json);
            AIRY_FREE(handle->tasks[i]);
        }
    }
    AIRY_FREE(handle);
}

int agntcy_agent_register(agntcy_handle_t *h, const agntcy_agent_card_t *card)
{
    if (!h || !card)
        return AIRY_ERR_NULL_POINTER;
    if (h->agent_count >= AGNTCY_ACP_MAX_AGENTS)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    for (size_t i = 0; i < h->agent_count; i++) {
        if (strcmp(h->agents[i].agent_id, card->agent_id) == 0) {
            h->agents[i] = *card;
            h->agents[i].registered_at = (uint64_t)time(NULL);
            h->agents[i].online = true;
            return 0;
        }
    }

    h->agents[h->agent_count] = *card;
    h->agents[h->agent_count].registered_at = (uint64_t)time(NULL);
    h->agents[h->agent_count].online = true;
    h->agent_count++;
    return 0;
}

int agntcy_agent_unregister(agntcy_handle_t *h, const char *agent_id)
{
    if (!h || !agent_id)
        return AIRY_ERR_NULL_POINTER;

    for (size_t i = 0; i < h->agent_count; i++) {
        if (strcmp(h->agents[i].agent_id, agent_id) == 0) {
            if (i < h->agent_count - 1) {
                __builtin_memmove(&h->agents[i], &h->agents[i + 1],
                        (h->agent_count - i - 1) * sizeof(agntcy_agent_card_t));
            }
            h->agent_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int agntcy_agent_discover(agntcy_handle_t *h, uint32_t cap_mask, agntcy_agent_card_t *results,
                          size_t *count)
{
    if (!h || !results || !count)
        return AIRY_ERR_NULL_POINTER;

    size_t found = 0;
    for (size_t i = 0; i < h->agent_count && found < *count; i++) {
        if (!h->agents[i].online)
            continue;
        if (cap_mask == 0 || (h->agents[i].capabilities_mask & cap_mask)) {
            results[found++] = h->agents[i];
        }
    }

    *count = found;
    return 0;
}

int agntcy_channel_open(agntcy_handle_t *h, const char *initiator_id, const char *responder_id,
                        agntcy_channel_t *channel)
{
    if (!h || !initiator_id || !responder_id || !channel)
        return AIRY_ERR_NULL_POINTER;
    if (h->channel_count >= AGNTCY_ACP_MAX_CHANNELS)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    bool initiator_found = false, responder_found = false;
    for (size_t i = 0; i < h->agent_count; i++) {
        if (strcmp(h->agents[i].agent_id, initiator_id) == 0 && h->agents[i].online)
            initiator_found = true;
        if (strcmp(h->agents[i].agent_id, responder_id) == 0 && h->agents[i].online)
            responder_found = true;
    }

    if (!initiator_found || !responder_found)
        AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");

    snprintf(channel->channel_id, AGNTCY_ACP_CHANNEL_ID_SIZE, "ch-%s-%s-%llu", initiator_id,
             responder_id, (unsigned long long)(uint64_t)(time(NULL) * 1000));

    unsigned char rnd[AGNTCY_ACP_TOKEN_SIZE - 1];
    airy_random_bytes(rnd, sizeof(rnd));
    for (size_t i = 0; i < AGNTCY_ACP_TOKEN_SIZE - 1; i++) {
        channel->session_token[i] = (char)('a' + (rnd[i] % 26));
    }
    channel->session_token[AGNTCY_ACP_TOKEN_SIZE - 1] = '\0';

    AIRY_STRNCPY_TERM(channel->initiator_id, initiator_id, sizeof(channel->initiator_id));
    AIRY_STRNCPY_TERM(channel->responder_id, responder_id, sizeof(channel->responder_id));
    channel->established_at = (uint64_t)time(NULL);
    channel->expires_at = channel->established_at + 3600;
    channel->encrypted = true;

    h->channels[h->channel_count++] = *channel;
    return 0;
}

int agntcy_channel_close(agntcy_handle_t *h, const char *channel_id)
{
    if (!h || !channel_id)
        return AIRY_ERR_NULL_POINTER;

    for (size_t i = 0; i < h->channel_count; i++) {
        if (strcmp(h->channels[i].channel_id, channel_id) == 0) {
            if (i < h->channel_count - 1) {
                __builtin_memmove(&h->channels[i], &h->channels[i + 1],
                        (h->channel_count - i - 1) * sizeof(agntcy_channel_t));
            }
            h->channel_count--;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int agntcy_message_send(agntcy_handle_t *h, const agntcy_message_t *msg, char *response,
                        size_t *resp_size)
{
    if (!h || !msg || !response || !resp_size)
        return AIRY_ERR_NULL_POINTER;

    h->message_counter++;

    bool channel_valid = false;
    if (msg->channel_id[0] != '\0') {
        for (size_t i = 0; i < h->channel_count; i++) {
            if (strcmp(h->channels[i].channel_id, msg->channel_id) == 0) {
                channel_valid = true;
                break;
            }
        }
    } else {
        channel_valid = true;
    }

    if (!channel_valid)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    if (msg->payload && msg->payload_size > 0) {
        int written = snprintf(response, *resp_size,
                               "{"
                               "\"message_id\":\"%s\","
                               "\"status\":\"delivered\","
                               "\"sender\":\"%s\","
                               "\"receiver\":\"%s\","
                               "\"payload_size\":%zu,"
                               "\"protocol\":\"agntcy-acp-%s\""
                               "}",
                               msg->message_id, msg->sender_id, msg->receiver_id, msg->payload_size,
                               AGNTCY_ACP_VERSION);
        if (written > 0)
            *resp_size = (size_t)written;
    } else {
        int written = snprintf(response, *resp_size, "{\"status\":\"ack\",\"message_id\":\"%s\"}",
                               msg->message_id);
        if (written > 0)
            *resp_size = (size_t)written;
    }

    return 0;
}

int agntcy_message_broadcast(agntcy_handle_t *h, const agntcy_message_t *msg)
{
    if (!h || !msg)
        return AIRY_ERR_NULL_POINTER;

    h->message_counter++;

    for (size_t i = 0; i < h->agent_count; i++) {
        if (h->agents[i].online && strcmp(h->agents[i].agent_id, msg->sender_id) != 0) {
            (void)i;
        }
    }

    return 0;
}

int agntcy_task_orchestrate(agntcy_handle_t *h, const char *task_id, const char *workflow_json)
{
    if (!h || !task_id || !workflow_json)
        return AIRY_ERR_NULL_POINTER;
    if (h->task_count >= AGNTCY_ACP_MAX_TASKS)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    agntcy_task_t *task = NULL;
    for (size_t i = 0; i < h->task_count; i++) {
        if (strcmp(h->tasks[i]->task_id, task_id) == 0) {
            task = h->tasks[i];
            break;
        }
    }

    if (!task) {
        task = (agntcy_task_t *)AIRY_CALLOC(1, sizeof(agntcy_task_t));
        if (!task)
            AIRY_ERROR(AIRY_ERR_NULL_POINTER, "null pointer");
        AIRY_STRNCPY_TERM(task->task_id, task_id, sizeof(task->task_id));
        h->tasks[h->task_count++] = task;
    }

    AIRY_FREE(task->workflow_json);
    task->workflow_json = AIRY_STRDUP(workflow_json);
    AIRY_STRNCPY_TERM(task->name, "orchestrated-task", sizeof(task->name));
    task->state = AGNTCY_TASK_DISPATCHED;
    task->created_at = (uint64_t)time(NULL);
    task->deadline_at = task->created_at + 3600;
    task->priority = 5;

    for (size_t i = 0; i < h->agent_count && task->assigned_count < AGNTCY_ACP_MAX_AGENTS; i++) {
        if (h->agents[i].online && (h->agents[i].capabilities_mask & AGNTCY_CAP_ORCHESTRATE)) {
            AIRY_STRNCPY_TERM(task->assigned_agent_ids[task->assigned_count], h->agents[i].agent_id, sizeof(task->assigned_agent_ids[task->assigned_count]));
            task->assigned_count++;
        }
    }

    h->task_counter++;
    return 0;
}

int agntcy_task_get_state(agntcy_handle_t *h, const char *task_id, agntcy_task_state_t *state)
{
    if (!h || !task_id || !state)
        return AIRY_ERR_NULL_POINTER;

    for (size_t i = 0; i < h->task_count; i++) {
        if (strcmp(h->tasks[i]->task_id, task_id) == 0) {
            *state = h->tasks[i]->state;
            return 0;
        }
    }
    AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");
}

int agntcy_ack_negotiate(agntcy_handle_t *h, const char *agent_id, const agntcy_ack_t *ack_request,
                         agntcy_ack_t *ack_response)
{
    if (!h || !agent_id || !ack_request || !ack_response)
        {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "agntcy_ack_negotiate: failed");
        return AIRY_ERR_UNKNOWN;
        }

    bool agent_found = false;
    for (size_t i = 0; i < h->agent_count; i++) {
        if (strcmp(h->agents[i].agent_id, agent_id) == 0 && h->agents[i].online) {
            agent_found = true;
            break;
        }
    }
    if (!agent_found)
        AIRY_ERROR(AIRY_ERR_INVALID_PARAM, "invalid parameter");

    __builtin_memcpy(ack_response, ack_request, sizeof(agntcy_ack_t));

    ack_response->guaranteed_amount = ack_request->requested_amount;
    if (ack_request->requested_amount > (size_t)(1024 * 1024 * 1024)) {
        ack_response->guaranteed_amount = (size_t)(1024 * 1024 * 1024);
    }

    ack_response->cpu_cores = ack_request->cpu_cores;
    if (ack_response->cpu_cores < 1)
        ack_response->cpu_cores = 1;

    ack_response->valid_until = (uint64_t)time(NULL) + 3600;
    ack_response->committed = true;

    return 0;
}

static int agntcy_proto_init(void *context, const void *config)
{
    (void)config;
    agntcy_handle_t *h = (agntcy_handle_t *)context;
    if (!h) {
        if (agntcy_acp_create(&h) != 0)
            return AIRY_ERR_OUT_OF_MEMORY;
    }
    g_agntcy_state.handle = *h;
    g_agntcy_state.proto_initialized = true;
    return 0;
}

static int agntcy_proto_init_adapter(void *context)
{
    return agntcy_proto_init(context, NULL);
}

static int agntcy_proto_destroy(void *context)
{
    (void)context;
    g_agntcy_state.proto_initialized = false;
    g_agntcy_state.is_connected = false;
    AIRY_FREE(g_agntcy_state.connected_endpoint);
    g_agntcy_state.connected_endpoint = NULL;
    AIRY_FREE(g_agntcy_state.send_buffer);
    g_agntcy_state.send_buffer = NULL;
    g_agntcy_state.send_buffer_size = 0;
    return 0;
}

static int agntcy_proto_handle_request(void *context, const void *req, void **resp)
{
    (void)context;
    if (!req || !resp)
        return AIRY_ERR_NULL_POINTER;

    const char *__attribute__((unused)) raw = (const char *)req;
    char buf[4096];
    snprintf(buf, sizeof(buf),
             "{"
             "\"protocol\":\"agntcy-acp\","
             "\"version\":\"%s\","
             "\"agents_registered\":%zu,"
             "\"active_channels\":%zu,"
             "\"tasks_active\":%zu,"
             "\"messages_total\":%llu"
             "}",
             AGNTCY_ACP_VERSION, g_agntcy_state.handle.agent_count,
             g_agntcy_state.handle.channel_count, g_agntcy_state.handle.task_count,
             (unsigned long long)g_agntcy_state.handle.message_counter);

    *resp = AIRY_STRDUP(buf);
    return 0;
}

static const char *agntcy_proto_get_version(void *context)
{
    (void)context;
    return AGNTCY_ACP_VERSION;
}

static int agntcy_proto_get_version_adapter(void *context, char *version_buf, size_t max_size)
{
    const char *ver = agntcy_proto_get_version(context);
    if (!version_buf || max_size == 0)
        return AIRY_ERR_INVALID_PARAM;
    snprintf(version_buf, max_size, "%s", ver);
    return 0;
}

static uint64_t agntcy_proto_capabilities(void *context)
{
    (void)context;
    return (uint64_t)(AGNTCY_CAP_DISCOVERY | AGNTCY_CAP_CHANNEL | AGNTCY_CAP_MESSAGING |
                      AGNTCY_CAP_ORCHESTRATE | AGNTCY_CAP_BROADCAST | AGNTCY_CAP_ACK);
}

static uint32_t agntcy_proto_capabilities_adapter(void *context)
{
    return (uint32_t)agntcy_proto_capabilities(context);
}

static int agntcy_proto_encode(void *context, const void *msg, void **out_data, size_t *out_size)
{
    (void)context;
    if (!msg || !out_data || !out_size)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_encode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    const unified_message_t *umsg = (const unified_message_t *)msg;
    const char *payload = umsg->payload ? (const char *)umsg->payload : "";
    size_t payload_len = umsg->payload_size;
    size_t buf_size = 256 + payload_len;
    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf)
        {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "agntcy_proto_encode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    int written = snprintf(buf, buf_size,
                           "{\"protocol\":%d,\"direction\":%d,\"timestamp\":%llu,\"payload\":\"%.*s\"}",
                           (int)umsg->protocol, (int)umsg->direction,
                           (unsigned long long)umsg->timestamp, (int)payload_len, payload);
    if (written < 0)
        {
        AIRY_FREE(buf);
        airy_err_push_ex(AIRY_ERR_IO, __FILE__, __LINE__, __func__, "agntcy_proto_encode: snprintf failed");
        return AIRY_ERR_IO;
        }
    *out_data = buf;
    *out_size = (size_t)written;
    return 0;
}

static int agntcy_proto_decode(void *context, const void *data, size_t size, void *out_msg)
{
    (void)context;
    if (!data || !out_msg)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    if (size == 0)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_decode: zero size");
        return AIRY_ERR_INVALID_PARAM;
        }

    unified_message_t *msg = (unified_message_t *)out_msg;
    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy)
        {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "agntcy_proto_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    msg->protocol = AIRY_PROTOCOL_AGNTCY;
    char *p = strstr(copy, "\"protocol\":");
    if (p)
        msg->protocol = (airy_protocol_type_t)strtol(p + 11, NULL, 10);

    msg->direction = DIRECTION_RESPONSE;
    p = strstr(copy, "\"direction\":");
    if (p)
        msg->direction = (message_direction_t)strtol(p + 12, NULL, 10);

    msg->timestamp = (uint64_t)time(NULL);
    p = strstr(copy, "\"timestamp\":");
    if (p)
        msg->timestamp = (uint64_t)strtoull(p + 12, NULL, 10);

    p = strstr(copy, "\"payload\":\"");
    if (p) {
        p += 11;
        char *end = strchr(p, '"');
        size_t plen = end ? (size_t)(end - p) : strlen(p);
        msg->payload = AIRY_MALLOC(plen + 1);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, p, plen);
            ((char *)msg->payload)[plen] = '\0';
            msg->payload_size = plen;
        }
    } else {
        msg->payload = AIRY_STRDUP("");
        msg->payload_size = 0;
    }

    AIRY_FREE(copy);
    return 0;
}

static int agntcy_proto_connect(void *context, const char *endpoint)
{
    (void)context;
    AIRY_FREE(g_agntcy_state.connected_endpoint);
    g_agntcy_state.connected_endpoint = endpoint ? AIRY_STRDUP(endpoint) : NULL;
    g_agntcy_state.is_connected = true;
    return 0;
}

static int agntcy_proto_disconnect(void *context)
{
    (void)context;
    g_agntcy_state.is_connected = false;
    AIRY_FREE(g_agntcy_state.connected_endpoint);
    g_agntcy_state.connected_endpoint = NULL;
    return 0;
}

static int agntcy_proto_is_connected(void *context)
{
    (void)context;
    return g_agntcy_state.is_connected ? 1 : 0;
}

static int agntcy_proto_send(void *context, const void *data, size_t size)
{
    (void)context;
    if (!data)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_send: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    AIRY_FREE(g_agntcy_state.send_buffer);
    g_agntcy_state.send_buffer = AIRY_MALLOC(size + 1);
    if (!g_agntcy_state.send_buffer)
        {
        g_agntcy_state.send_buffer_size = 0;
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "agntcy_proto_send: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
        }
    __builtin_memcpy(g_agntcy_state.send_buffer, data, size);
    ((char *)g_agntcy_state.send_buffer)[size] = '\0';
    g_agntcy_state.send_buffer_size = size;
    g_agntcy_state.bytes_sent += size;
    return 0;
}

static int agntcy_proto_receive(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    if (!data || !size)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_receive: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    if (!g_agntcy_state.send_buffer || g_agntcy_state.send_buffer_size == 0)
        {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "agntcy_proto_receive: no data");
        return AIRY_ERR_TIMEOUT;
        }
    *data = g_agntcy_state.send_buffer;
    *size = g_agntcy_state.send_buffer_size;
    g_agntcy_state.bytes_received += *size;
    g_agntcy_state.send_buffer = NULL;
    g_agntcy_state.send_buffer_size = 0;
    return 0;
}

static int agntcy_proto_get_stats(void *context, char *stats_json, size_t max_size)
{
    (void)context;
    if (!stats_json || max_size < 64)
        {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__, "agntcy_proto_get_stats: invalid param");
        return AIRY_ERR_INVALID_PARAM;
        }
    int written = snprintf(stats_json, max_size,
                           "{\"adapter\":\"agntcy_acp\",\"version\":\"%s\",\"connected\":%s,"
                           "\"bytes_sent\":%llu,\"bytes_received\":%llu,"
                           "\"agents\":%zu,\"channels\":%zu,"
                           "\"tasks\":%zu,\"messages\":%llu}",
                           AGNTCY_ACP_VERSION, g_agntcy_state.is_connected ? "true" : "false",
                           (unsigned long long)g_agntcy_state.bytes_sent,
                           (unsigned long long)g_agntcy_state.bytes_received,
                           g_agntcy_state.handle.agent_count,
                           g_agntcy_state.handle.channel_count,
                           g_agntcy_state.handle.task_count,
                           (unsigned long long)g_agntcy_state.handle.message_counter);
    return (written >= 0 && (size_t)written < max_size) ? 0 : AIRY_ERR_BUFFER_TOO_SMALL;
}

const proto_adapter_t *agntcy_get_protocol_adapter(void)
{
    static proto_adapter_t adapter = {0};
    static bool initialized = false;

    if (!initialized) {
        adapter.name = "AGNTCY ACP";
        adapter.version = AGNTCY_ACP_VERSION;
        adapter.description =
            "Agent Communication Protocol - open standard for agent-to-agent communication";
        adapter.type = AIRY_PROTOCOL_AGNTCY;  /* P0-15: PROTO_AGNTCY→枚举值 */
        adapter.init = agntcy_proto_init_adapter;
        adapter.destroy = agntcy_proto_destroy;
        adapter.encode = agntcy_proto_encode;
        adapter.decode = agntcy_proto_decode;
        adapter.connect = agntcy_proto_connect;
        adapter.disconnect = agntcy_proto_disconnect;
        adapter.is_connected = agntcy_proto_is_connected;
        adapter.send = agntcy_proto_send;
        adapter.receive = agntcy_proto_receive;
        adapter.handle_request = agntcy_proto_handle_request;
        adapter.get_version = agntcy_proto_get_version_adapter;
        adapter.capabilities = agntcy_proto_capabilities_adapter;
        adapter.get_stats = agntcy_proto_get_stats;
        initialized = true;
    }

    return &adapter;
}
