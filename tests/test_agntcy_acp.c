// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_agntcy_acp.c
 * @brief AGNTCY ACP协议适配器单元测试
 */
// @owner: team-B

#include "agntcy_acp_adapter.h"
#include "logging.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_create_destroy(void)
{
    agntcy_handle_t *h = NULL;
    int ret __attribute__((unused)) = agntcy_acp_create(&h);
    assert(ret == 0);
    assert(h != NULL);
    assert(h->initialized == true);
    assert(h->agent_count == 0);
    assert(h->channel_count == 0);
    assert(h->task_count == 0);
    agntcy_acp_destroy(h);
    return 0;
}

static int test_create_null(void)
{
    int ret __attribute__((unused)) = agntcy_acp_create(NULL);
    assert(ret != 0);
    return 0;
}

static int test_agent_register(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t card = {0};
    snprintf(card.agent_id, sizeof(card.agent_id), "%s", "agent-001");
    snprintf(card.name, sizeof(card.name), "%s", "TestAgent");
    card.capabilities_mask = AGNTCY_CAP_DISCOVERY | AGNTCY_CAP_MESSAGING;
    snprintf(card.endpoint_url, sizeof(card.endpoint_url), "%s", "http://localhost:9001");

    int ret __attribute__((unused)) = agntcy_agent_register(h, &card);
    assert(ret == 0);
    assert(h->agent_count == 1);
    assert(strcmp(h->agents[0].agent_id, "agent-001") == 0);
    assert(h->agents[0].online == true);

    ret = agntcy_agent_register(NULL, &card);
    assert(ret != 0);

    ret = agntcy_agent_register(h, NULL);
    assert(ret != 0);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_agent_register_duplicate(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t card = {0};
    snprintf(card.agent_id, sizeof(card.agent_id), "%s", "agent-001");
    snprintf(card.name, sizeof(card.name), "%s", "TestAgent");
    card.capabilities_mask = AGNTCY_CAP_DISCOVERY;

    agntcy_agent_register(h, &card);
    card.capabilities_mask |= AGNTCY_CAP_CHANNEL;
    agntcy_agent_register(h, &card);

    assert(h->agent_count == 1);
    assert(h->agents[0].capabilities_mask & AGNTCY_CAP_CHANNEL);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_agent_unregister(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t card1 = {0}, card2 = {0};
    snprintf(card1.agent_id, sizeof(card1.agent_id), "%s", "agent-001");
    card1.capabilities_mask = AGNTCY_CAP_DISCOVERY;
    snprintf(card2.agent_id, sizeof(card2.agent_id), "%s", "agent-002");
    card2.capabilities_mask = AGNTCY_CAP_CHANNEL;

    agntcy_agent_register(h, &card1);
    agntcy_agent_register(h, &card2);
    assert(h->agent_count == 2);

    int ret __attribute__((unused)) = agntcy_agent_unregister(h, "agent-001");
    assert(ret == 0);
    assert(h->agent_count == 1);
    assert(strcmp(h->agents[0].agent_id, "agent-002") == 0);

    ret = agntcy_agent_unregister(h, "not-exist");
    assert(ret != 0);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_agent_discover(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t card1 = {0}, card2 = {0};
    snprintf(card1.agent_id, sizeof(card1.agent_id), "%s", "agent-disc");
    card1.capabilities_mask = AGNTCY_CAP_DISCOVERY | AGNTCY_CAP_CHANNEL;
    snprintf(card1.endpoint_url, sizeof(card1.endpoint_url), "%s", "http://host:9001");
    card1.online = true;

    snprintf(card2.agent_id, sizeof(card2.agent_id), "%s", "agent-msg");
    card2.capabilities_mask = AGNTCY_CAP_MESSAGING;
    snprintf(card2.endpoint_url, sizeof(card2.endpoint_url), "%s", "http://host:9002");
    card2.online = true;

    agntcy_agent_register(h, &card1);
    agntcy_agent_register(h, &card2);

    agntcy_agent_card_t results[10];
    size_t count = 10;

    int ret __attribute__((unused)) = agntcy_agent_discover(h, AGNTCY_CAP_CHANNEL, results, &count);
    assert(ret == 0);
    assert(count == 1);
    assert(strcmp(results[0].agent_id, "agent-disc") == 0);

    count = 10;
    ret = agntcy_agent_discover(h, 0, results, &count);
    assert(ret == 0);
    assert(count == 2);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_channel_open_close(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t initiator = {0}, responder = {0};
    snprintf(initiator.agent_id, sizeof(initiator.agent_id), "%s", "init-01");
    initiator.capabilities_mask = AGNTCY_CAP_CHANNEL;
    initiator.online = true;
    snprintf(responder.agent_id, sizeof(responder.agent_id), "%s", "resp-01");
    responder.capabilities_mask = AGNTCY_CAP_CHANNEL;
    responder.online = true;

    agntcy_agent_register(h, &initiator);
    agntcy_agent_register(h, &responder);

    agntcy_channel_t channel = {0};
    int ret __attribute__((unused)) = agntcy_channel_open(h, "init-01", "resp-01", &channel);
    assert(ret == 0);
    assert(channel.encrypted == true);
    assert(strlen(channel.session_token) == AGNTCY_ACP_TOKEN_SIZE - 1);
    assert(h->channel_count == 1);

    ret = agntcy_channel_open(h, "nobody", "resp-01", &channel);
    assert(ret == -3);

    ret = agntcy_channel_close(h, channel.channel_id);
    assert(ret == 0);
    assert(h->channel_count == 0);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_message_send(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t snd = {0}, rcv = {0};
    snprintf(snd.agent_id, sizeof(snd.agent_id), "%s", "sender");
    snd.online = true;
    snprintf(rcv.agent_id, sizeof(rcv.agent_id), "%s", "receiver");
    rcv.online = true;
    agntcy_agent_register(h, &snd);
    agntcy_agent_register(h, &rcv);

    agntcy_channel_t ch = {0};
    agntcy_channel_open(h, "sender", "receiver", &ch);

    agntcy_message_t msg = {0};
    snprintf(msg.message_id, sizeof(msg.message_id), "%s", "msg-001");
    snprintf(msg.sender_id, sizeof(msg.sender_id), "%s", "sender");
    snprintf(msg.receiver_id, sizeof(msg.receiver_id), "%s", "receiver");
    snprintf(msg.channel_id, sizeof(msg.channel_id), "%s", ch.channel_id);
    msg.mode = AGNTCY_MSG_SYNC;
    msg.payload = (char *)"Hello";
    msg.payload_size = 5;
    msg.timestamp = 1000;

    char resp[4096] = {0};
    size_t resp_size = sizeof(resp);

    int ret __attribute__((unused)) = agntcy_message_send(h, &msg, resp, &resp_size);
    assert(ret == 0);
    assert(resp_size > 0);
    assert(strstr(resp, "msg-001") != NULL);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_task_orchestrate(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t worker = {0};
    snprintf(worker.agent_id, sizeof(worker.agent_id), "%s", "worker-01");
    worker.capabilities_mask = AGNTCY_CAP_ORCHESTRATE;
    worker.online = true;
    agntcy_agent_register(h, &worker);

    int ret __attribute__((unused)) =
        agntcy_task_orchestrate(h, "task-001",
                                "{\"steps\":[{\"id\":\"step1\",\"action\":\"process\"}]}");
    assert(ret == 0);
    assert(h->task_count == 1);
    assert(h->tasks[0]->state == AGNTCY_TASK_DISPATCHED);
    assert(h->tasks[0]->assigned_count >= 1);

    agntcy_task_state_t state;
    ret = agntcy_task_get_state(h, "task-001", &state);
    assert(ret == 0);
    assert(state == AGNTCY_TASK_DISPATCHED);

    ret = agntcy_task_get_state(h, "no-task", &state);
    assert(ret != 0);

    agntcy_acp_destroy(h);
    return 0;
}

static int test_ack_negotiate(void)
{
    agntcy_handle_t *h = NULL;
    agntcy_acp_create(&h);

    agntcy_agent_card_t agent = {0};
    snprintf(agent.agent_id, sizeof(agent.agent_id), "%s", "agent-ack");
    agent.online = true;
    agntcy_agent_register(h, &agent);

    agntcy_ack_t req = {0}, resp = {0};
    snprintf(req.resource_type, sizeof(req.resource_type), "%s", "memory");
    req.requested_amount = 500 * 1024 * 1024;
    req.cpu_cores = 2;

    int ret __attribute__((unused)) = agntcy_ack_negotiate(h, "agent-ack", &req, &resp);
    assert(ret == 0);
    assert(resp.committed == true);
    assert(resp.cpu_cores == 2);
    assert(resp.valid_until > 0);

    ret = agntcy_ack_negotiate(h, "nobody", &req, &resp);
    assert(ret != 0);

    agntcy_acp_destroy(h);
    return 0;
}

int main(void)
{
    int failures = 0;

#define RUN_TEST(name)              \
    printf("[TEST] %s... ", #name); \
    if (test_##name() != 0) {       \
        printf("FAILED\n");         \
        failures++;                 \
    } else                          \
        printf("PASSED\n")

    RUN_TEST(create_destroy);
    RUN_TEST(create_null);
    RUN_TEST(agent_register);
    RUN_TEST(agent_register_duplicate);
    RUN_TEST(agent_unregister);
    RUN_TEST(agent_discover);
    RUN_TEST(channel_open_close);
    RUN_TEST(message_send);
    RUN_TEST(task_orchestrate);
    RUN_TEST(ack_negotiate);

    AIRY_LOG_INFO("\nAGNTCY ACP tests: %d failures", failures);
    return failures;
}
