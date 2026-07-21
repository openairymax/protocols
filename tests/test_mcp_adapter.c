// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/**
 * @file test_mcp_adapter.c
 * @brief MCP v1.0 Protocol Adapter Unit Tests
 * @copyright (c) 2026 SPHARX. All Rights Reserved.
 */
// @owner: team-B

#include "mcp_v1_adapter.h"

#include <assert.h>
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                              \
    do {                                        \
        LOG_INFO("  TEST: %s ... ", name); \
    } while (0)
#define PASS()            \
    do {                  \
        LOG_INFO("PASS"); \
        tests_passed++;   \
    } while (0)
#define FAIL(msg)                  \
    do {                           \
        LOG_ERROR("FAIL: %s", msg); \
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
    mcp_v1_config_t cfg = mcp_v1_config_default();
    ASSERT_TRUE(cfg.default_timeout_ms > 0, "default timeout should be positive");
    ASSERT_TRUE(cfg.max_tools > 0, "max tools should be positive");
    ASSERT_TRUE(cfg.max_resources > 0, "max resources should be positive");
    ASSERT_TRUE(cfg.max_message_size > 0, "max message size should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    mcp_v1_context_t *ctx = mcp_v1_context_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(MCP_V1_VERSION) > 0, "MCP version should not be empty");
    ASSERT_TRUE(strlen(MCP_V1_PROTOCOL_NAME) > 0, "protocol name should not be empty");
    ASSERT_TRUE(MCP_V1_MAX_TOOLS > 0, "max tools should be positive");
    ASSERT_TRUE(MCP_V1_MAX_RESOURCES > 0, "max resources should be positive");
    ASSERT_TRUE(MCP_V1_MAX_PROMPTS > 0, "max prompts should be positive");
    ASSERT_TRUE(MCP_V1_DEFAULT_TIMEOUT_MS > 0, "default timeout should be positive");
    PASS();
}

static void test_register_tool_null(void)
{
    TEST("register_tool with NULL context returns error");
    mcp_tool_t tool = {0};
    tool.name = "test_tool";
    int rc = mcp_v1_register_tool(NULL, &tool, NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_tool with NULL should fail");
    PASS();
}

static void test_register_resource_null(void)
{
    TEST("register_resource with NULL context returns error");
    mcp_resource_t res = {0};
    res.uri = "test://resource";
    int rc = mcp_v1_register_resource(NULL, &res, NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_resource with NULL should fail");
    PASS();
}

static void test_register_prompt_null(void)
{
    TEST("register_prompt with NULL context returns error");
    mcp_prompt_t prompt = {0};
    prompt.name = "test_prompt";
    int rc = mcp_v1_register_prompt(NULL, &prompt, NULL, NULL);
    ASSERT_TRUE(rc != 0, "register_prompt with NULL should fail");
    PASS();
}

static void test_set_sampling_handler_null(void)
{
    TEST("set_sampling_handler with NULL context returns error");
    int rc = mcp_v1_set_sampling_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_sampling_handler with NULL should fail");
    PASS();
}

static void test_set_completion_handler_null(void)
{
    TEST("set_completion_handler with NULL context returns error");
    int rc = mcp_v1_set_completion_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_completion_handler with NULL should fail");
    PASS();
}

static void test_set_log_level_null(void)
{
    TEST("set_log_level with NULL context returns error");
    int rc = mcp_v1_set_log_level(NULL, MCP_LOG_INFO);
    ASSERT_TRUE(rc != 0, "set_log_level with NULL should fail");
    PASS();
}

static void test_set_log_callback_null(void)
{
    TEST("set_log_callback with NULL context returns error");
    int rc = mcp_v1_set_log_callback(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_log_callback with NULL should fail");
    PASS();
}

static void test_set_progress_callback_null(void)
{
    TEST("set_progress_callback with NULL context returns error");
    int rc = mcp_v1_set_progress_callback(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_progress_callback with NULL should fail");
    PASS();
}

static void test_handle_tools_list_null(void)
{
    TEST("handle_tools_list with NULL context returns error");
    int rc = mcp_v1_handle_tools_list(NULL, NULL);
    ASSERT_TRUE(rc != 0, "handle_tools_list with NULL should fail");
    PASS();
}

static void test_handle_tools_call_null(void)
{
    TEST("handle_tools_call with NULL context returns error");
    int rc = mcp_v1_handle_tools_call(NULL, "tool", "{}", NULL);
    ASSERT_TRUE(rc != 0, "handle_tools_call with NULL should fail");
    PASS();
}

static void test_handle_resources_list_null(void)
{
    TEST("handle_resources_list with NULL context returns error");
    int rc = mcp_v1_handle_resources_list(NULL, NULL);
    ASSERT_TRUE(rc != 0, "handle_resources_list with NULL should fail");
    PASS();
}

static void test_handle_resources_read_null(void)
{
    TEST("handle_resources_read with NULL context returns error");
    int rc = mcp_v1_handle_resources_read(NULL, "test://uri", NULL);
    ASSERT_TRUE(rc != 0, "handle_resources_read with NULL should fail");
    PASS();
}

static void test_handle_prompts_list_null(void)
{
    TEST("handle_prompts_list with NULL context returns error");
    int rc = mcp_v1_handle_prompts_list(NULL, NULL);
    ASSERT_TRUE(rc != 0, "handle_prompts_list with NULL should fail");
    PASS();
}

static void test_handle_prompts_get_null(void)
{
    TEST("handle_prompts_get with NULL context returns error");
    int rc = mcp_v1_handle_prompts_get(NULL, "prompt", "{}", NULL);
    ASSERT_TRUE(rc != 0, "handle_prompts_get with NULL should fail");
    PASS();
}

static void test_route_request_null(void)
{
    TEST("route_request with NULL context returns error");
    int rc = mcp_v1_route_request(NULL, "tools/list", "{}", NULL);
    ASSERT_TRUE(rc != 0, "route_request with NULL should fail");
    PASS();
}

static void test_tool_lifecycle(void)
{
    TEST("register and count tools");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    ASSERT_TRUE(mcp_v1_get_tool_count(ctx) == 0, "initial tool count should be 0");

    mcp_tool_t tool1 = {0};
    tool1.name = "tool1";
    tool1.description = "First tool";
    tool1.input_schema_json = "{\"type\":\"object\"}";
    tool1.required_caps = MCP_CAP_TOOLS;

    int rc = mcp_v1_register_tool(ctx, &tool1, NULL, NULL);
    ASSERT_TRUE(rc == 0, "register tool1 should succeed");
    ASSERT_TRUE(mcp_v1_get_tool_count(ctx) == 1, "tool count should be 1");

    mcp_tool_t tool2 = {0};
    tool2.name = "tool2";
    tool2.description = "Second tool";
    tool2.input_schema_json = "{\"type\":\"object\"}";
    tool2.required_caps = MCP_CAP_TOOLS;

    rc = mcp_v1_register_tool(ctx, &tool2, NULL, NULL);
    ASSERT_TRUE(rc == 0, "register tool2 should succeed");
    ASSERT_TRUE(mcp_v1_get_tool_count(ctx) == 2, "tool count should be 2");

    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_resource_lifecycle(void)
{
    TEST("register and count resources");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    ASSERT_TRUE(mcp_v1_get_resource_count(ctx) == 0, "initial resource count should be 0");

    mcp_resource_t res = {0};
    res.uri = "file:///data/test.txt";
    res.name = "Test Resource";
    res.description = "A test resource";
    res.mime_type = "text/plain";

    int rc = mcp_v1_register_resource(ctx, &res, NULL, NULL);
    ASSERT_TRUE(rc == 0, "register resource should succeed");
    ASSERT_TRUE(mcp_v1_get_resource_count(ctx) == 1, "resource count should be 1");

    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_prompt_lifecycle(void)
{
    TEST("register and count prompts");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    ASSERT_TRUE(mcp_v1_get_prompt_count(ctx) == 0, "initial prompt count should be 0");

    mcp_prompt_t prompt = {0};
    prompt.name = "greeting";
    prompt.description = "Greeting prompt";
    prompt.arguments_schema_json = "{\"type\":\"object\"}";

    int rc = mcp_v1_register_prompt(ctx, &prompt, NULL, NULL);
    ASSERT_TRUE(rc == 0, "register prompt should succeed");
    ASSERT_TRUE(mcp_v1_get_prompt_count(ctx) == 1, "prompt count should be 1");

    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_capabilities(void)
{
    TEST("get capabilities");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    cfg.capabilities = MCP_CAP_TOOLS | MCP_CAP_RESOURCES | MCP_CAP_PROMPTS;
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    uint32_t caps = mcp_v1_get_capabilities(ctx);
    ASSERT_TRUE((caps & MCP_CAP_TOOLS) != 0, "should have tools capability");
    ASSERT_TRUE((caps & MCP_CAP_RESOURCES) != 0, "should have resources capability");
    ASSERT_TRUE((caps & MCP_CAP_PROMPTS) != 0, "should have prompts capability");

    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_set_log_level(void)
{
    TEST("set log level");
    mcp_v1_config_t cfg = mcp_v1_config_default();
    mcp_v1_context_t *ctx = mcp_v1_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    int rc = mcp_v1_set_log_level(ctx, MCP_LOG_WARNING);
    ASSERT_TRUE(rc == 0, "set log level to warning should succeed");

    rc = mcp_v1_set_log_level(ctx, MCP_LOG_DEBUG);
    ASSERT_TRUE(rc == 0, "set log level to debug should succeed");

    mcp_v1_context_destroy(ctx);
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    mcp_content_destroy(NULL, 0);
    mcp_sampling_result_destroy(NULL);
    mcp_completion_result_destroy(NULL);
    PASS();
}

static void test_stream_event_init(void)
{
    TEST("stream event init");
    mcp_stream_event_t event;
    mcp_stream_event_init(&event, MCP_STREAM_EVENT_CONTENT, "chunk data");
    ASSERT_TRUE(event.type == MCP_STREAM_EVENT_CONTENT, "event type should be content");
    ASSERT_NOT_NULL(event.event_data, "event data should not be NULL");
    /* P0-02 修复: mcp_stream_event_init() 内部通过 AIRY_STRDUP(data) 分配 event_data，
     * 调用方负责释放。这里直接 free() 避免引入 airy_memory.h 依赖。 */
    free(event.event_data);
    event.event_data = NULL;
    PASS();
}

static void test_stream_event_type_string(void)
{
    TEST("stream event type string");
    const char *s = mcp_stream_event_type_string(MCP_STREAM_EVENT_CONTENT);
    ASSERT_NOT_NULL(s, "content type string should be non-NULL");
    ASSERT_TRUE(strlen(s) > 0, "content type string should not be empty");

    s = mcp_stream_event_type_string(MCP_STREAM_EVENT_ERROR);
    ASSERT_NOT_NULL(s, "error type string should be non-NULL");

    s = mcp_stream_event_type_string(MCP_STREAM_EVENT_DONE);
    ASSERT_NOT_NULL(s, "done type string should be non-NULL");

    PASS();
}

static void test_send_progress_null(void)
{
    TEST("send_progress with NULL context returns error");
    int rc = mcp_v1_send_progress(NULL, "token-1", 0.5, 1.0);
    ASSERT_TRUE(rc != 0, "send_progress with NULL should fail");
    PASS();
}

static void test_notify_cancelled_null(void)
{
    TEST("notify_cancelled with NULL context returns error");
    int rc = mcp_v1_notify_cancelled(NULL, "req-1", "test");
    ASSERT_TRUE(rc != 0, "notify_cancelled with NULL should fail");
    PASS();
}

static void test_stream_config_null(void)
{
    TEST("stream_config with NULL context returns error");
    mcp_stream_config_t sc = {0};
    sc.enabled = true;
    int rc = mcp_v1_stream_config(NULL, &sc);
    ASSERT_TRUE(rc != 0, "stream_config with NULL should fail");
    PASS();
}

static void test_handle_tools_call_streaming_null(void)
{
    TEST("handle_tools_call_streaming with NULL context returns error");
    int rc = mcp_v1_handle_tools_call_streaming(NULL, "tool", "{}", NULL, NULL);
    ASSERT_TRUE(rc != 0, "handle_tools_call_streaming with NULL should fail");
    PASS();
}

static void test_handle_sampling_streaming_null(void)
{
    TEST("handle_sampling_streaming with NULL context returns error");
    mcp_sampling_params_t params = {0};
    int rc = mcp_v1_handle_sampling_streaming(NULL, &params, NULL, NULL);
    ASSERT_TRUE(rc != 0, "handle_sampling_streaming with NULL should fail");
    PASS();
}

int main(void)
{
    LOG_INFO("=== MCP v1.0 Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_version_constants();
    test_register_tool_null();
    test_register_resource_null();
    test_register_prompt_null();
    test_set_sampling_handler_null();
    test_set_completion_handler_null();
    test_set_log_level_null();
    test_set_log_callback_null();
    test_set_progress_callback_null();
    test_handle_tools_list_null();
    test_handle_tools_call_null();
    test_handle_resources_list_null();
    test_handle_resources_read_null();
    test_handle_prompts_list_null();
    test_handle_prompts_get_null();
    test_route_request_null();
    test_tool_lifecycle();
    test_resource_lifecycle();
    test_prompt_lifecycle();
    test_capabilities();
    test_set_log_level();
    test_destroy_handlers_null();
    test_stream_event_init();
    test_stream_event_type_string();
    test_send_progress_null();
    test_notify_cancelled_null();
    test_stream_config_null();
    test_handle_tools_call_streaming_null();
    test_handle_sampling_streaming_null();

    LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}