// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_langchain_adapter.c
 * @brief LangChain Framework Adapter Unit Tests
 */
// @owner: team-B

#include "langchain_adapter.h"

#include <assert.h>
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                         \
    do {                                   \
        LOG_INFO("  TEST: %s ... ", name); \
    } while (0)
#define PASS()            \
    do {                  \
        LOG_INFO("PASS"); \
        tests_passed++;   \
    } while (0)
#define FAIL(msg)                   \
    do {                            \
        LOG_ERROR("FAIL: %s", msg); \
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
    langchain_config_t cfg = langchain_config_default();
    ASSERT_TRUE(cfg.timeout_ms > 0, "timeout should be positive");
    ASSERT_TRUE(cfg.max_concurrent_chains > 0, "max concurrent chains should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    langchain_adapter_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    langchain_adapter_context_t *ctx = langchain_adapter_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_is_initialized(void)
{
    TEST("is_initialized after create");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");
    bool init = langchain_adapter_is_initialized(ctx);
    ASSERT_TRUE(init, "adapter after create should be initialized");
    langchain_adapter_destroy(ctx);
    PASS();
}

static void test_is_initialized_null(void)
{
    TEST("is_initialized with NULL returns false");
    bool init = langchain_adapter_is_initialized(NULL);
    ASSERT_TRUE(!init, "NULL context should return false");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(LANGCHAIN_ADAPTER_VERSION) > 0, "adapter version should not be empty");
    ASSERT_TRUE(LANGCHAIN_MAX_CHAINS > 0, "max chains should be positive");
    ASSERT_TRUE(LANGCHAIN_MAX_TOOLS > 0, "max tools should be positive");
    ASSERT_TRUE(LANGCHAIN_MAX_AGENTS > 0, "max agents should be positive");
    ASSERT_TRUE(LANGCHAIN_MAX_MEMORY_ENTRIES > 0, "max memory entries should be positive");
    ASSERT_TRUE(LANGCHAIN_DEFAULT_TIMEOUT_MS > 0, "default timeout should be positive");
    PASS();
}

static void test_register_tool_null(void)
{
    TEST("register_tool with NULL context returns error");
    langchain_tool_def_t tool = {0};
    tool.name = "test_tool";
    int rc = langchain_register_tool(NULL, &tool, NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_tool with NULL should fail");
    PASS();
}

static void test_list_tools_null(void)
{
    TEST("list_tools with NULL context returns error");
    langchain_tool_def_t *tools = NULL;
    size_t count = 0;
    int rc = langchain_list_tools(NULL, &tools, &count);
    ASSERT_TRUE(rc != 0, "list_tools with NULL should fail");
    PASS();
}

static void test_create_chain_null(void)
{
    TEST("create_chain with NULL context returns error");
    langchain_chain_def_t chain = {0};
    chain.name = "test_chain";
    langchain_chain_instance_t instance = {0};
    int rc = langchain_create_chain(NULL, &chain, &instance);
    ASSERT_TRUE(rc != 0, "create_chain with NULL should fail");
    PASS();
}

static void test_execute_chain_null(void)
{
    TEST("execute_chain with NULL context returns error");
    langchain_execution_result_t result = {0};
    int rc = langchain_execute_chain(NULL, "chain-1", "{}", &result);
    ASSERT_TRUE(rc != 0, "execute_chain with NULL should fail");
    PASS();
}

static void test_execute_chain_streaming_null(void)
{
    TEST("execute_chain_streaming with NULL context returns error");
    int rc = langchain_execute_chain_streaming(NULL, "chain-1", "{}", NULL, NULL);
    ASSERT_TRUE(rc != 0, "execute_chain_streaming with NULL should fail");
    PASS();
}

static void test_create_agent_null(void)
{
    TEST("create_agent with NULL context returns error");
    langchain_agent_def_t agent = {0};
    agent.name = "test_agent";
    int rc = langchain_create_agent(NULL, &agent, NULL);
    ASSERT_TRUE(rc != 0, "create_agent with NULL should fail");
    PASS();
}

static void test_agent_run_null(void)
{
    TEST("agent_run with NULL context returns error");
    langchain_execution_result_t result = {0};
    int rc = langchain_agent_run(NULL, "agent-1", "task", &result);
    ASSERT_TRUE(rc != 0, "agent_run with NULL should fail");
    PASS();
}

static void test_create_memory_null(void)
{
    TEST("create_memory with NULL context returns error");
    langchain_memory_t mem = {0};
    int rc = langchain_create_memory(NULL, LC_MEM_BUFFER, 100, &mem);
    ASSERT_TRUE(rc != 0, "create_memory with NULL should fail");
    PASS();
}

static void test_memory_add_null(void)
{
    TEST("memory_add with NULL context returns error");
    int rc = langchain_memory_add(NULL, "mem-1", "user", "hello");
    ASSERT_TRUE(rc != 0, "memory_add with NULL should fail");
    PASS();
}

static void test_memory_get_null(void)
{
    TEST("memory_get with NULL context returns error");
    langchain_memory_t snapshot = {0};
    int rc = langchain_memory_get(NULL, "mem-1", &snapshot);
    ASSERT_TRUE(rc != 0, "memory_get with NULL should fail");
    PASS();
}

static void test_set_streaming_handler_null(void)
{
    TEST("set_streaming_handler with NULL context returns error");
    int rc = langchain_set_streaming_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_streaming_handler with NULL should fail");
    PASS();
}

static void test_set_trace_handler_null(void)
{
    TEST("set_trace_handler with NULL context returns error");
    int rc = langchain_set_trace_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_trace_handler with NULL should fail");
    PASS();
}

static void test_set_llm_callback_null(void)
{
    TEST("set_llm_callback with NULL context returns error");
    int rc = langchain_set_llm_callback(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_llm_callback with NULL should fail");
    PASS();
}

static void test_get_statistics_null(void)
{
    TEST("get_statistics with NULL context returns error");
    char buf[256] = {0};
    int rc = langchain_get_statistics(NULL, buf, sizeof(buf));
    ASSERT_TRUE(rc != 0, "get_statistics with NULL should fail");
    PASS();
}

static void test_tool_lifecycle(void)
{
    TEST("register and list tools");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    langchain_tool_def_t tool = {0};
    tool.id = "tool-001";
    tool.name = "calculator";
    tool.description = "A calculator tool";
    tool.function_schema_json = "{\"type\":\"object\"}";
    tool.tool_type = LC_TYPE_TOOL;

    int rc = langchain_register_tool(ctx, &tool, NULL, NULL);
    ASSERT_TRUE(rc == 0, "register tool should succeed");

    langchain_tool_def_t *tools = NULL;
    size_t count = 0;
    rc = langchain_list_tools(ctx, &tools, &count);
    ASSERT_TRUE(rc == 0, "list tools should succeed");
    ASSERT_TRUE(count >= 1, "tool count should be at least 1");

    langchain_adapter_destroy(ctx);

    /* P0-11 fix: langchain_list_tools() returns a heap copy of the tools array;
      * id/name/description/function_schema_json are STRDUP'd; callers own them.
      * Historical bug: the test leaked list_tools' result; ASAN found 40B(array)
      * + 56B of 194B from 4x STRDUP leaks.
      * Free each tool's string fields via langchain_tool_def_destroy(), then the array. */
    for (size_t i = 0; i < count; i++)
        langchain_tool_def_destroy(&tools[i]);
    free(tools);
    PASS();
}

static void test_chain_lifecycle(void)
{
    TEST("create chain lifecycle");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    langchain_chain_def_t chain = {0};
    chain.id = "chain-001";
    chain.name = "TestChain";
    chain.type = LC_CHAIN_SEQUENTIAL;

    langchain_chain_instance_t instance = {0};
    int rc = langchain_create_chain(ctx, &chain, &instance);
    ASSERT_TRUE(rc == 0, "create chain should succeed");
    ASSERT_TRUE(instance.is_compiled, "chain should be compiled");

    langchain_adapter_destroy(ctx);

    /* P0-12 fix: instance.id/input_schema_json/output_schema_json from
      * langchain_create_chain() are STRDUP'd; callers own them.
      * Historical bug: the test leaked the instance; ASAN found
     * 18B(id "lc-chain-...") + 3B(input_schema_json "{}") + 3B(output_schema_json "{}")
      * a 24B leak. Free via langchain_chain_instance_destroy(). */
    langchain_chain_instance_destroy(&instance);
    PASS();
}

static void test_agent_lifecycle(void)
{
    TEST("create agent lifecycle");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    langchain_agent_def_t agent = {0};
    agent.id = "agent-001";
    agent.name = "TestAgent";
    agent.type = LC_AGENT_REACT;
    agent.max_iterations = 5;
    agent.max_execution_time_sec = 60;

    char out_id[64] = {0};
    int rc = langchain_create_agent(ctx, &agent, out_id);
    ASSERT_TRUE(rc == 0, "create agent should succeed");
    ASSERT_TRUE(strlen(out_id) > 0, "agent ID should not be empty");

    langchain_adapter_destroy(ctx);
    PASS();
}

static void test_memory_lifecycle(void)
{
    TEST("create and use memory");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    langchain_memory_t mem = {0};
    int rc = langchain_create_memory(ctx, LC_MEM_BUFFER, 100, &mem);
    ASSERT_TRUE(rc == 0, "create memory should succeed");
    ASSERT_TRUE(mem.type == LC_MEM_BUFFER, "memory type should be buffer");
    ASSERT_TRUE(strlen(mem.id) > 0, "memory ID should not be empty");

    rc = langchain_memory_add(ctx, mem.id, "user", "Hello");
    ASSERT_TRUE(rc == 0, "memory add should succeed");

    langchain_memory_t snapshot = {0};
    rc = langchain_memory_get(ctx, mem.id, &snapshot);
    ASSERT_TRUE(rc == 0, "memory get should succeed");
    ASSERT_TRUE(snapshot.message_count >= 1, "message count should be at least 1");

    langchain_adapter_destroy(ctx);

    /* P0-13 fix: mem.id from langchain_create_memory() is STRDUP'd;
      * the snapshot from langchain_memory_get() holds id/messages-array/string copies;
      * 都是 STRDUP 拷贝，调用方负责释放。
      * Historical bug: the test leaked them; ASAN found
      * 16B(mem.id) + 16B(snapshot.id) + 8B(messages array) + 34B(messages[0]
      * JSON) = 74B leaks.
      * Free via langchain_memory_destroy(). */
    langchain_memory_destroy(&mem);
    langchain_memory_destroy(&snapshot);
    PASS();
}

static void test_adapter_version(void)
{
    TEST("adapter_version returns valid string");
    langchain_config_t cfg = langchain_config_default();
    langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    const char *ver = langchain_adapter_version();
    ASSERT_NOT_NULL(ver, "version should not be NULL");
    ASSERT_TRUE(strlen(ver) > 0, "version should not be empty");

    langchain_adapter_destroy(ctx);
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    langchain_tool_def_destroy(NULL);
    langchain_chain_def_destroy(NULL);
    langchain_chain_instance_destroy(NULL);
    langchain_agent_def_destroy(NULL);
    langchain_memory_destroy(NULL);
    langchain_execution_result_destroy(NULL);
    PASS();
}

int main(void)
{
    LOG_INFO("=== LangChain Framework Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_is_initialized();
    test_is_initialized_null();
    test_version_constants();
    test_register_tool_null();
    test_list_tools_null();
    test_create_chain_null();
    test_execute_chain_null();
    test_execute_chain_streaming_null();
    test_create_agent_null();
    test_agent_run_null();
    test_create_memory_null();
    test_memory_add_null();
    test_memory_get_null();
    test_set_streaming_handler_null();
    test_set_trace_handler_null();
    test_set_llm_callback_null();
    test_get_statistics_null();
    test_tool_lifecycle();
    test_chain_lifecycle();
    test_agent_lifecycle();
    test_memory_lifecycle();
    test_adapter_version();
    test_destroy_handlers_null();

    LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}