// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/**
 * @file test_openai_adapter.c
 * @brief OpenAI Enterprise Adapter Unit Tests
 * @copyright (c) 2026 SPHARX. All Rights Reserved.
 */
// @owner: team-B

#include "openai_enterprise_adapter.h"

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
    openai_enterprise_config_t cfg = openai_enterprise_config_default();
    ASSERT_TRUE(cfg.request_timeout_ms > 0, "request timeout should be positive");
    ASSERT_TRUE(cfg.max_retries > 0, "max retries should be positive");
    ASSERT_TRUE(cfg.max_tokens_default > 0, "max tokens default should be positive");
    PASS();
}

static void test_context_create_destroy(void)
{
    TEST("context create and destroy");
    openai_enterprise_config_t cfg = openai_enterprise_config_default();
    openai_enterprise_context_t *ctx = openai_enterprise_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should return non-NULL");
    openai_enterprise_context_destroy(ctx);
    PASS();
}

static void test_context_create_null_config(void)
{
    TEST("context create with NULL config returns NULL");
    openai_enterprise_context_t *ctx = openai_enterprise_context_create(NULL);
    ASSERT_TRUE(ctx == NULL, "create with NULL config should return NULL");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(OPENAI_ADAPTER_VERSION) > 0, "adapter version should not be empty");
    ASSERT_TRUE(OPENAI_MAX_MODELS > 0, "max models should be positive");
    ASSERT_TRUE(OPENAI_MAX_FUNCTIONS > 0, "max functions should be positive");
    ASSERT_TRUE(OPENAI_MAX_MESSAGES > 0, "max messages should be positive");
    ASSERT_TRUE(OPENAI_MAX_TOKENS_DEFAULT > 0, "max tokens default should be positive");
    ASSERT_TRUE(OPENAI_RATE_LIMIT_RPM > 0, "rate limit RPM should be positive");
    PASS();
}

static void test_register_model_null(void)
{
    TEST("register_model with NULL context returns error");
    openai_model_t model = {0};
    model.id = "gpt-4";
    model.name = "GPT-4";
    int rc = openai_enterprise_register_model(NULL, &model);
    ASSERT_TRUE(rc != 0, "register_model with NULL should fail");
    PASS();
}

static void test_chat_completion_null(void)
{
    TEST("chat_completion with NULL context returns error");
    int rc = openai_enterprise_chat_completion(NULL, "gpt-4", NULL, 0, NULL, 0, 0.0, 0.0, 0, NULL);
    ASSERT_TRUE(rc != 0, "chat_completion with NULL should fail");
    PASS();
}

static void test_chat_streaming_null(void)
{
    TEST("chat_streaming with NULL context returns error");
    int rc = openai_enterprise_chat_streaming(NULL, "gpt-4", NULL, 0, NULL, NULL);
    ASSERT_TRUE(rc != 0, "chat_streaming with NULL should fail");
    PASS();
}

static void test_embeddings_null(void)
{
    TEST("embeddings with NULL context returns error");
    const char *inputs[] = {"test"};
    int rc = openai_enterprise_embeddings(NULL, "text-embedding", inputs, 1, NULL);
    ASSERT_TRUE(rc != 0, "embeddings with NULL should fail");
    PASS();
}

static void test_list_models_null(void)
{
    TEST("list_models with NULL context returns error");
    openai_model_t *models = NULL;
    size_t count = 0;
    int rc = openai_enterprise_list_models(NULL, &models, &count);
    ASSERT_TRUE(rc != 0, "list_models with NULL should fail");
    PASS();
}

static void test_check_rate_limit_null(void)
{
    TEST("check_rate_limit with NULL context returns false");
    bool ok = openai_enterprise_check_rate_limit(NULL, 100);
    ASSERT_TRUE(!ok, "check_rate_limit with NULL should return false");
    PASS();
}

static void test_set_chat_handler_null(void)
{
    TEST("set_chat_handler with NULL context returns error");
    int rc = openai_enterprise_set_chat_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_chat_handler with NULL should fail");
    PASS();
}

static void test_set_embedding_handler_null(void)
{
    TEST("set_embedding_handler with NULL context returns error");
    int rc = openai_enterprise_set_embedding_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_embedding_handler with NULL should fail");
    PASS();
}

static void test_set_audit_handler_null(void)
{
    TEST("set_audit_handler with NULL context returns error");
    int rc = openai_enterprise_set_audit_handler(NULL, NULL, NULL);
    ASSERT_TRUE(rc != 0, "set_audit_handler with NULL should fail");
    PASS();
}

static void test_route_request_null(void)
{
    TEST("route_request with NULL context returns error");
    int rc = openai_enterprise_route_request(NULL, "/v1/chat/completions", "POST", "{}", NULL);
    ASSERT_TRUE(rc != 0, "route_request with NULL should fail");
    PASS();
}

static void test_model_register(void)
{
    TEST("register model lifecycle");
    openai_enterprise_config_t cfg = openai_enterprise_config_default();
    openai_enterprise_context_t *ctx = openai_enterprise_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    openai_model_t model = {0};
    model.id = "gpt-4";
    model.name = "GPT-4";
    model.owned_by = "openai";
    model.capabilities = OPENAI_MODEL_CHAT | OPENAI_MODEL_FUNCTION;
    model.max_context_tokens = 8192;
    model.max_output_tokens = 4096;
    model.is_default = true;
    model.is_available = true;

    int rc = openai_enterprise_register_model(ctx, &model);
    ASSERT_TRUE(rc == 0, "register model should succeed");

    openai_model_t *models = NULL;
    size_t count = 0;
    rc = openai_enterprise_list_models(ctx, &models, &count);
    ASSERT_TRUE(rc == 0, "list models should succeed");
    ASSERT_TRUE(count >= 1, "model count should be at least 1");

    openai_enterprise_context_destroy(ctx);
    PASS();
}

static void test_rate_limit(void)
{
    TEST("rate limit check");
    openai_enterprise_config_t cfg = openai_enterprise_config_default();
    cfg.enable_rate_limiting = true;
    cfg.rpm_limit = 60;
    cfg.tpm_limit = 100000;
    openai_enterprise_context_t *ctx = openai_enterprise_context_create(&cfg);
    ASSERT_NOT_NULL(ctx, "create should succeed");

    bool ok = openai_enterprise_check_rate_limit(ctx, 100);
    ASSERT_TRUE(ok, "rate limit check should pass for small request");

    openai_enterprise_context_destroy(ctx);
    PASS();
}

static void test_destroy_handlers_null(void)
{
    TEST("destroy handlers with NULL do not crash");
    openai_chat_response_destroy(NULL);
    openai_embedding_response_destroy(NULL);
    openai_message_destroy(NULL);
    openai_model_destroy(NULL);
    PASS();
}

int main(void)
{
    AIRY_LOG_INFO("=== OpenAI Enterprise Adapter Unit Tests ===\n\n");

    test_config_default();
    test_context_create_destroy();
    test_context_create_null_config();
    test_version_constants();
    test_register_model_null();
    test_chat_completion_null();
    test_chat_streaming_null();
    test_embeddings_null();
    test_list_models_null();
    test_check_rate_limit_null();
    test_set_chat_handler_null();
    test_set_embedding_handler_null();
    test_set_audit_handler_null();
    test_route_request_null();
    test_model_register();
    test_rate_limit();
    test_destroy_handlers_null();

    AIRY_LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}