// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_claude_adapter.c
 * @brief Anthropic Claude API Adapter Unit Tests
 */
// @owner: team-B

#include "claude_adapter.h"

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
    claude_config_t cfg = claude_config_default();
    ASSERT_TRUE(cfg.timeout_ms > 0, "timeout should be positive");
    ASSERT_TRUE(cfg.max_retries > 0, "max retries should be positive");
    ASSERT_TRUE(cfg.max_tokens > 0, "max tokens should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    claude_config_t cfg = claude_config_default();
    claude_adapter_context_t *ctx = claude_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    claude_adapter_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    claude_adapter_context_t *ctx = claude_adapter_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_is_initialized(void)
{
    TEST("is_initialized after create");
    claude_config_t cfg = claude_config_default();
    claude_adapter_context_t *ctx = claude_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");
    bool init = claude_adapter_is_initialized(ctx);
    ASSERT_TRUE(init, "adapter after create should be initialized");
    claude_adapter_destroy(ctx);
    PASS();
}

static void test_is_initialized_null(void)
{
    TEST("is_initialized with NULL returns false");
    bool init = claude_adapter_is_initialized(NULL);
    ASSERT_TRUE(!init, "NULL context should return false");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(CLAUDE_ADAPTER_VERSION) > 0, "adapter version should not be empty");
    ASSERT_TRUE(strlen(CLAUDE_API_VERSION) > 0, "API version should not be empty");
    ASSERT_TRUE(CLAUDE_MAX_MODELS > 0, "max models should be positive");
    ASSERT_TRUE(CLAUDE_MAX_TOOLS > 0, "max tools should be positive");
    ASSERT_TRUE(CLAUDE_MAX_MESSAGES > 0, "max messages should be positive");
    ASSERT_TRUE(CLAUDE_MAX_CONTEXT_TOKENS > 0, "max context tokens should be positive");
    ASSERT_TRUE(CLAUDE_DEFAULT_TIMEOUT_MS > 0, "default timeout should be positive");
    PASS();
}

static void test_messages_create_null(void)
{
    TEST("messages_create with NULL context returns error");
    int rc = claude_messages_create(NULL, NULL, 0, NULL, 0, NULL, NULL);
    ASSERT_TRUE(rc != 0, "messages_create with NULL should fail");
    PASS();
}

static void test_messages_stream_null(void)
{
    TEST("messages_stream with NULL context returns error");
    int rc = claude_messages_stream(NULL, NULL, 0, NULL, 0, NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "messages_stream with NULL should fail");
    PASS();
}

static void test_count_tokens_null(void)
{
    TEST("count_tokens with NULL context returns error");
    int count = 0;
    int rc = claude_count_tokens(NULL, NULL, 0, NULL, &count);
    ASSERT_TRUE(rc != 0, "count_tokens with NULL should fail");
    PASS();
}

static void test_list_models_null(void)
{
    TEST("list_models with NULL context returns error");
    claude_model_info_t *models = NULL;
    size_t count = 0;
    int rc = claude_list_models(NULL, &models, &count);
    ASSERT_TRUE(rc != 0, "list_models with NULL should fail");
    PASS();
}

static void test_set_message_handler_null(void)
{
    TEST("set_message_handler with NULL context returns error");
    int rc = claude_set_message_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_message_handler with NULL should fail");
    PASS();
}

static void test_set_stream_handler_null(void)
{
    TEST("set_stream_handler with NULL context returns error");
    int rc = claude_set_stream_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_stream_handler with NULL should fail");
    PASS();
}

static void test_set_tool_use_handler_null(void)
{
    TEST("set_tool_use_handler with NULL context returns error");
    int rc = claude_set_tool_use_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_tool_use_handler with NULL should fail");
    PASS();
}

static void test_get_usage_statistics_null(void)
{
    TEST("get_usage_statistics with NULL context returns error");
    char buf[256] = {0};
    int rc = claude_get_usage_statistics(NULL, buf, sizeof(buf));
    ASSERT_TRUE(rc != 0, "get_usage_statistics with NULL should fail");
    PASS();
}

static void test_adapter_version(void)
{
    TEST("adapter_version returns valid string");
    claude_config_t cfg = claude_config_default();
    claude_adapter_context_t *ctx = claude_adapter_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    const char *ver = claude_adapter_version();
    ASSERT_NOT_NULL(ver, "version should not be NULL");
    ASSERT_TRUE(strlen(ver) > 0, "version should not be empty");

    claude_adapter_destroy(ctx);
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    claude_response_destroy(NULL);
    claude_message_destroy(NULL);
    claude_tool_def_destroy(NULL);
    claude_model_info_destroy(NULL);
    claude_stream_event_destroy(NULL);
    PASS();
}

int main(void)
{
    LOG_INFO("=== Claude API Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_is_initialized();
    test_is_initialized_null();
    test_version_constants();
    test_messages_create_null();
    test_messages_stream_null();
    test_count_tokens_null();
    test_list_models_null();
    test_set_message_handler_null();
    test_set_stream_handler_null();
    test_set_tool_use_handler_null();
    test_get_usage_statistics_null();
    test_adapter_version();
    test_destroy_handlers_null();

    LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}