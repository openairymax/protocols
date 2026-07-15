// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/**
 * @file test_autogen_adapter.c
 * @brief AutoGen Framework Adapter Unit Tests
 * @copyright (c) 2026 SPHARX. All Rights Reserved.
 */
// @owner: team-B

#include "autogen_adapter.h"

#include <assert.h>
#include "logging_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                              \
    do {                                        \
        AIRY_LOG_INFO("  TEST: %s ... ", name); \
    } while (0)
#define PASS()            \
    do {                  \
        AIRY_LOG_INFO("PASS"); \
        tests_passed++;   \
    } while (0)
#define FAIL(msg)                  \
    do {                           \
        AIRY_LOG_ERROR("FAIL: %s", msg); \
        tests_failed++;            \
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
    autogen_config_t cfg = autogen_config_default();
    ASSERT_TRUE(cfg.timeout_ms > 0, "timeout should be positive");
    ASSERT_TRUE(cfg.max_agents_per_group > 0, "max agents per group should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    autogen_adapter_context_t *ctx = autogen_adapter_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_is_initialized(void)
{
    TEST("is_initialized after create");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");
    bool init = autogen_adapter_is_initialized(ctx);
    ASSERT_TRUE(init, "adapter after create should be initialized");
    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_is_initialized_null(void)
{
    TEST("is_initialized with NULL returns false");
    bool init = autogen_adapter_is_initialized(NULL);
    ASSERT_TRUE(!init, "NULL context should return false");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(AUTOGEN_ADAPTER_VERSION) > 0, "adapter version should not be empty");
    ASSERT_TRUE(AUTOGEN_MAX_AGENTS > 0, "max agents should be positive");
    ASSERT_TRUE(AUTOGEN_MAX_GROUP_CHATS > 0, "max group chats should be positive");
    ASSERT_TRUE(AUTOGEN_MAX_MESSAGES > 0, "max messages should be positive");
    ASSERT_TRUE(AUTOGEN_MAX_TOOLS > 0, "max tools should be positive");
    ASSERT_TRUE(AUTOGEN_MAX_RESPONSE_LEN > 0, "max response len should be positive");
    ASSERT_TRUE(AUTOGEN_DEFAULT_TIMEOUT_MS > 0, "default timeout should be positive");
    PASS();
}

static void test_create_agent_null(void)
{
    TEST("create_agent with NULL context returns error");
    autogen_agent_def_t agent = {0};
    agent.name = "test_agent";
    int rc = autogen_create_agent(NULL, &agent, NULL);
    ASSERT_TRUE(rc != 0, "create_agent with NULL should fail");
    PASS();
}

static void test_destroy_agent_null(void)
{
    TEST("destroy_agent with NULL context returns error");
    int rc = autogen_destroy_agent(NULL, "agent-1");
    ASSERT_TRUE(rc != 0, "destroy_agent with NULL should fail");
    PASS();
}

static void test_list_agents_null(void)
{
    TEST("list_agents with NULL context returns error");
    autogen_agent_instance_t *agents = NULL;
    size_t count = 0;
    int rc = autogen_list_agents(NULL, &agents, &count);
    ASSERT_TRUE(rc != 0, "list_agents with NULL should fail");
    PASS();
}

static void test_create_group_chat_null(void)
{
    TEST("create_group_chat with NULL context returns error");
    autogen_group_chat_def_t gc = {0};
    gc.name = "test_group";
    int rc = autogen_create_group_chat(NULL, &gc, NULL);
    ASSERT_TRUE(rc != 0, "create_group_chat with NULL should fail");
    PASS();
}

static void test_initiate_chat_null(void)
{
    TEST("initiate_chat with NULL context returns error");
    autogen_group_chat_result_t result = {0};
    int rc = autogen_initiate_chat(NULL, "group-1", "agent-1", "hello", &result);
    ASSERT_TRUE(rc != 0, "initiate_chat with NULL should fail");
    PASS();
}

static void test_send_message_null(void)
{
    TEST("send_message with NULL context returns error");
    autogen_message_t reply = {0};
    int rc = autogen_send_message(NULL, "agent-1", "agent-2", "hello", MSG_TYPE_TEXT, &reply);
    ASSERT_TRUE(rc != 0, "send_message with NULL should fail");
    PASS();
}

static void test_register_tool_null(void)
{
    TEST("register_tool with NULL context returns error");
    int rc = autogen_register_tool(NULL, "tool", "desc", "{}", NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_tool with NULL should fail");
    PASS();
}

static void test_get_conversation_null(void)
{
    TEST("get_conversation with NULL context returns error");
    autogen_conversation_t conv = {0};
    int rc = autogen_get_conversation(NULL, "group-1", &conv);
    ASSERT_TRUE(rc != 0, "get_conversation with NULL should fail");
    PASS();
}

static void test_set_code_executor_null(void)
{
    TEST("set_code_executor with NULL context returns error");
    int rc = autogen_set_code_executor(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_code_executor with NULL should fail");
    PASS();
}

static void test_set_human_callback_null(void)
{
    TEST("set_human_callback with NULL context returns error");
    int rc = autogen_set_human_callback(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_human_callback with NULL should fail");
    PASS();
}

static void test_set_message_hook_null(void)
{
    TEST("set_message_hook with NULL context returns error");
    int rc = autogen_set_message_hook(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_message_hook with NULL should fail");
    PASS();
}

static void test_set_llm_callback_null(void)
{
    TEST("set_llm_callback with NULL context returns error");
    int rc = autogen_set_llm_callback(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_llm_callback with NULL should fail");
    PASS();
}

static void test_get_statistics_null(void)
{
    TEST("get_statistics with NULL context returns error");
    char buf[256] = {0};
    int rc = autogen_get_statistics(NULL, buf, sizeof(buf));
    ASSERT_TRUE(rc != 0, "get_statistics with NULL should fail");
    PASS();
}

static void test_agent_lifecycle(void)
{
    TEST("create and destroy agent lifecycle");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    autogen_agent_def_t agent = {0};
    agent.name = "Assistant";
    agent.role = AGENT_ROLE_ASSISTANT;
    agent.system_message = "You are a helpful assistant.";
    agent.max_consecutive_auto_reply = 5;
    agent.is_termination = false;

    char out_id[64] = {0};
    int rc = autogen_create_agent(ctx, &agent, out_id);
    ASSERT_TRUE(rc == 0, "create agent should succeed");
    ASSERT_TRUE(strlen(out_id) > 0, "agent ID should not be empty");

    autogen_agent_instance_t *agents = NULL;
    size_t count = 0;
    rc = autogen_list_agents(ctx, &agents, &count);
    ASSERT_TRUE(rc == 0, "list agents should succeed");
    ASSERT_TRUE(count >= 1, "agent count should be at least 1");

    rc = autogen_destroy_agent(ctx, out_id);
    ASSERT_TRUE(rc == 0, "destroy agent should succeed");

    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_group_chat_lifecycle(void)
{
    TEST("create group chat lifecycle");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    /* Create agents first */
    autogen_agent_def_t agent = {0};
    agent.name = "Planner";
    agent.role = AGENT_ROLE_PLANNER;
    agent.max_consecutive_auto_reply = 3;

    char agent_id[64] = {0};
    int rc = autogen_create_agent(ctx, &agent, agent_id);
    ASSERT_TRUE(rc == 0, "create agent should succeed");

    /* Create group chat */
    autogen_group_chat_def_t gc = {0};
    gc.name = "TestGroup";
    gc.mode = GROUP_CHAT_ROUND_ROBIN;
    gc.max_rounds = 10;
    gc.allow_repeat_speaker = true;
    char *participants[] = {agent_id};
    gc.participant_ids = participants;
    gc.participant_count = 1;

    char out_id[64] = {0};
    rc = autogen_create_group_chat(ctx, &gc, out_id);
    ASSERT_TRUE(rc == 0, "create group chat should succeed");
    ASSERT_TRUE(strlen(out_id) > 0, "group chat ID should not be empty");

    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_tool_registration(void)
{
    TEST("register tool lifecycle");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    int rc = autogen_register_tool(ctx, "web_search", "Search the web",
                                   "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}}}",
                                   NULL, NULL);
    ASSERT_TRUE(rc == 0, "register tool should succeed");

    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_adapter_version(void)
{
    TEST("adapter_version returns valid string");
    autogen_config_t cfg = autogen_config_default();
    autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    const char *ver = autogen_adapter_version();
    ASSERT_NOT_NULL(ver, "version should not be NULL");
    ASSERT_TRUE(strlen(ver) > 0, "version should not be empty");

    autogen_adapter_destroy(ctx);
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    autogen_agent_def_destroy(NULL);
    autogen_agent_instance_destroy(NULL);
    autogen_group_chat_def_destroy(NULL);
    autogen_message_destroy(NULL);
    autogen_conversation_destroy(NULL);
    autogen_group_chat_result_destroy(NULL);
    PASS();
}

int main(void)
{
    AIRY_LOG_INFO("=== AutoGen Framework Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_is_initialized();
    test_is_initialized_null();
    test_version_constants();
    test_create_agent_null();
    test_destroy_agent_null();
    test_list_agents_null();
    test_create_group_chat_null();
    test_initiate_chat_null();
    test_send_message_null();
    test_register_tool_null();
    test_get_conversation_null();
    test_set_code_executor_null();
    test_set_human_callback_null();
    test_set_message_hook_null();
    test_set_llm_callback_null();
    test_get_statistics_null();
    test_agent_lifecycle();
    test_group_chat_lifecycle();
    test_tool_registration();
    test_adapter_version();
    test_destroy_handlers_null();

    AIRY_LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}