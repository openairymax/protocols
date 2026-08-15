// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_a2a_adapter.c
 * @brief A2A v0.3.0 Protocol Adapter Unit Tests
 */
// @owner: team-B

#include "a2a_v03_adapter.h"

#include <assert.h>
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                         \
    do {                                   \
        AIRY_LOG_INFO("  TEST: %s ... ", name); \
    } while (0)
#define PASS()            \
    do {                  \
        AIRY_LOG_INFO("PASS"); \
        tests_passed++;   \
    } while (0)
#define FAIL(msg)                   \
    do {                            \
        AIRY_LOG_ERROR("FAIL: %s", msg); \
        tests_failed++;             \
    } while (0)
#define ASSERT_TRUE(cond, msg) \
    do {                       \
        if (!(cond)) {         \
            FAIL(msg);         \
            return;            \
        }                      \
    } while (0)
#define ASSERT_NOT_NULL(p, msg) \
    do {                        \
        if ((p) == NULL) {      \
            FAIL(msg);          \
            return;             \
        }                       \
    } while (0)

static void test_config_default(void)
{
    TEST("config_default returns valid config");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    ASSERT_TRUE(cfg.default_timeout_ms > 0, "default timeout should be positive");
    ASSERT_TRUE(cfg.max_agents > 0, "max agents should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    a2a_v03_context_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    a2a_v03_context_t *ctx = a2a_v03_context_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_register_agent_null(void)
{
    TEST("register_agent with NULL context returns error");
    int rc = a2a_v03_register_agent(NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_agent with NULL should fail");
    PASS();
}

static void test_unregister_agent_null(void)
{
    TEST("unregister_agent with NULL context returns error");
    int rc = a2a_v03_unregister_agent(NULL, NULL);
    ASSERT_TRUE(rc != 0, "unregister_agent with NULL should fail");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(A2A_V03_VERSION) > 0, "A2A version should not be empty");
    ASSERT_TRUE(A2A_V03_MAX_AGENTS > 0, "max agents should be positive");
    ASSERT_TRUE(A2A_V03_MAX_TASKS > 0, "max tasks should be positive");
    ASSERT_TRUE(A2A_V03_MAX_MESSAGE_SIZE > 0, "max message size should be positive");
    ASSERT_TRUE(A2A_V03_DEFAULT_TIMEOUT_MS > 0, "default timeout should be positive");
    PASS();
}

static void test_get_agent_card_null(void)
{
    TEST("get_agent_card with NULL context returns NULL");
    const a2a_agent_card_t *card = a2a_v03_get_agent_card(NULL, "agent-1");
    ASSERT_TRUE(card == NULL, "NULL context should return NULL");
    PASS();
}

static void test_discover_agents_null(void)
{
    TEST("discover_agents with NULL context returns error");
    a2a_agent_card_t **results = NULL;
    size_t count = 0;
    int rc = a2a_v03_discover_agents(NULL, NULL, NULL, &results, &count);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_create_task_null(void)
{
    TEST("create_task with NULL context returns error");
    int rc = a2a_v03_create_task(NULL, "agent-1", "desc", "{}", NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_send_message_null(void)
{
    TEST("send_message with NULL context returns error");
    int rc = a2a_v03_send_message(NULL, "agent-1", NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_set_task_handler_null(void)
{
    TEST("set_task_handler with NULL context returns error");
    int rc = a2a_v03_set_task_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_set_message_handler_null(void)
{
    TEST("set_message_handler with NULL context returns error");
    int rc = a2a_v03_set_message_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_route_request_null(void)
{
    TEST("route_request with NULL context returns error");
    int rc = a2a_v03_route_request(NULL, "method", "{}", NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_register_and_unregister_agent(void)
{
    TEST("register and unregister agent");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    a2a_agent_card_t card = {0};
    card.id = "agent-test-01";
    card.name = "TestAgent";
    card.version = "1.0.0";
    card.capabilities = A2A_CAP_TASK_EXECUTION | A2A_CAP_MULTI_TURN;
    card.available = true;

    int rc = a2a_v03_register_agent(ctx, &card);
    ASSERT_TRUE(rc == 0, "register agent should succeed");
    ASSERT_TRUE(a2a_v03_get_agent_count(ctx) == 1, "agent count should be 1");

    const a2a_agent_card_t *fetched = a2a_v03_get_agent_card(ctx, "agent-test-01");
    ASSERT_NOT_NULL(fetched, "fetched agent should be non-NULL");
    ASSERT_TRUE(strcmp(fetched->id, "agent-test-01") == 0, "agent ID should match");

    rc = a2a_v03_unregister_agent(ctx, "agent-test-01");
    ASSERT_TRUE(rc == 0, "unregister agent should succeed");
    ASSERT_TRUE(a2a_v03_get_agent_count(ctx) == 0, "agent count should be 0");

    a2a_v03_context_destroy(ctx);
    PASS();
}

static void test_register_duplicate_agent(void)
{
    TEST("register duplicate agent updates existing");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    a2a_agent_card_t card1 = {0};
    card1.id = "dup-agent";
    card1.name = "First";
    card1.capabilities = A2A_CAP_TASK_EXECUTION;
    a2a_v03_register_agent(ctx, &card1);

    a2a_agent_card_t card2 = {0};
    card2.id = "dup-agent";
    card2.name = "Second";
    card2.capabilities = A2A_CAP_TASK_EXECUTION | A2A_CAP_STREAMING;
    a2a_v03_register_agent(ctx, &card2);

    ASSERT_TRUE(a2a_v03_get_agent_count(ctx) == 1, "duplicate should not increase count");
    const a2a_agent_card_t *fetched = a2a_v03_get_agent_card(ctx, "dup-agent");
    ASSERT_NOT_NULL(fetched, "duplicate agent should be fetchable");
    ASSERT_TRUE(fetched->capabilities & A2A_CAP_STREAMING, "capabilities should be merged");

    a2a_v03_context_destroy(ctx);
    PASS();
}

static void test_task_lifecycle(void)
{
    TEST("task create and cancel lifecycle");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    a2a_agent_card_t card = {0};
    card.id = "worker-01";
    card.name = "Worker";
    card.capabilities = A2A_CAP_TASK_EXECUTION;
    a2a_v03_register_agent(ctx, &card);

    a2a_task_t *task = NULL;
    int rc = a2a_v03_create_task(ctx, "worker-01", "Test task", "{\"key\":\"val\"}", &task);
    ASSERT_TRUE(rc == 0, "create task should succeed");
    ASSERT_NOT_NULL(task, "task should be non-NULL");
    ASSERT_TRUE(task->state == A2A_TASK_SUBMITTED, "task should be in submitted state");

    rc = a2a_v03_cancel_task(ctx, task->id, "Done testing");
    ASSERT_TRUE(rc == 0, "cancel task should succeed");

    a2a_task_t *cancelled = NULL;
    rc = a2a_v03_get_task(ctx, task->id, &cancelled);
    ASSERT_TRUE(rc == 0, "get cancelled task should succeed");
    ASSERT_TRUE(cancelled->state == A2A_TASK_CANCELED, "task should be canceled");

    a2a_v03_context_destroy(ctx);
    PASS();
}

static void test_get_task_nonexistent(void)
{
    TEST("get_task with nonexistent ID returns error");
    a2a_v03_config_t cfg = a2a_v03_config_default();
    a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    a2a_task_t *task = NULL;
    int rc = a2a_v03_get_task(ctx, "no-such-task", &task);
    ASSERT_TRUE(rc != 0, "nonexistent task should fail");

    a2a_v03_context_destroy(ctx);
    PASS();
}

static void test_negotiate_null(void)
{
    TEST("negotiate with NULL context returns error");
    int rc = a2a_v03_negotiate(NULL, NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "NULL context should fail");
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    a2a_agent_card_destroy(NULL);
    a2a_task_destroy(NULL);
    a2a_message_destroy(NULL);
    a2a_negotiation_destroy(NULL);
    PASS();
}

int main(void)
{
    AIRY_LOG_INFO("=== A2A v0.3.0 Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_version_constants();
    test_register_agent_null();
    test_unregister_agent_null();
    test_get_agent_card_null();
    test_discover_agents_null();
    test_create_task_null();
    test_send_message_null();
    test_set_task_handler_null();
    test_set_message_handler_null();
    test_route_request_null();
    test_negotiate_null();
    test_register_and_unregister_agent();
    test_register_duplicate_agent();
    test_task_lifecycle();
    test_get_task_nonexistent();
    test_destroy_handlers_null();

    AIRY_LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}