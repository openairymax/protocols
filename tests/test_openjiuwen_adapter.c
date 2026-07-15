// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/**
 * @file test_openjiuwen_adapter.c
 * @brief OpenJiuwen Protocol Adapter Unit Tests
 * @copyright (c) 2026 SPHARX. All Rights Reserved.
 */
// @owner: team-B

#include "openjiuwen_adapter.h"

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
    TEST("get_default_config returns valid config");
    openjiuwen_config_t cfg = {0};
    openjiuwen_get_default_config(&cfg);
    ASSERT_TRUE(strlen(cfg.endpoint) > 0, "endpoint should not be empty");
    ASSERT_TRUE(cfg.timeout_ms > 0, "timeout should be positive");
    PASS();
}

static void test_get_default_config_null(void)
{
    TEST("get_default_config with NULL does not crash");
    openjiuwen_get_default_config(NULL);
    PASS();
}

static void test_adapter_create(void)
{
    TEST("adapter_create returns valid adapter");
    openjiuwen_config_t cfg = {0};
    openjiuwen_get_default_config(&cfg);
    const protocol_adapter_t *adapter = openjiuwen_adapter_create(&cfg);
    ASSERT_NOT_NULL(adapter, "adapter_create should return non-NULL");
    ASSERT_NOT_NULL(adapter->name, "adapter name should not be NULL");
    ASSERT_NOT_NULL(adapter->version, "adapter version should not be NULL");
    PASS();
}

static void test_adapter_create_null_config(void)
{
    TEST("adapter_create with NULL config uses defaults");
    const protocol_adapter_t *adapter = openjiuwen_adapter_create(NULL);
    ASSERT_NOT_NULL(adapter, "adapter_create with NULL config should use defaults");
    PASS();
}

static void test_version_constants(void)
{
    TEST("version constants are valid");
    ASSERT_TRUE(strlen(OPENJIUWEN_PROTOCOL_VERSION) > 0, "protocol version should not be empty");
    ASSERT_TRUE(OPENJIUWEN_MAX_MESSAGE_SIZE > 0, "max message size should be positive");
    ASSERT_TRUE(OPENJIUWEN_TIMEOUT_MS > 0, "timeout should be positive");
    ASSERT_TRUE(OPENJIUWEN_HEARTBEAT_INTERVAL_SEC > 0, "heartbeat interval should be positive");
    ASSERT_TRUE(OPENJIUWEN_MAX_CONSECUTIVE_ERRORS > 0, "max consecutive errors should be positive");
    PASS();
}

static void test_verify_connection_null(void)
{
    TEST("verify_connection with NULL returns error");
    int rc = openjiuwen_verify_connection(NULL);
    ASSERT_TRUE(rc != 0, "verify_connection with NULL should fail");
    PASS();
}

static void test_get_capabilities_null(void)
{
    TEST("get_capabilities with NULL adapter returns error");
    char buf[256] = {0};
    int rc = openjiuwen_get_capabilities(NULL, buf, sizeof(buf));
    ASSERT_TRUE(rc != 0, "get_capabilities with NULL should fail");
    PASS();
}

static void test_get_capabilities(void)
{
    TEST("get_capabilities returns valid capabilities string");
    openjiuwen_config_t cfg = {0};
    openjiuwen_get_default_config(&cfg);
    const protocol_adapter_t *adapter = openjiuwen_adapter_create(&cfg);
    ASSERT_NOT_NULL(adapter, "adapter_create should succeed");

    char buf[256] = {0};
    int rc = openjiuwen_get_capabilities(adapter, buf, sizeof(buf));
    ASSERT_TRUE(rc == 0, "get_capabilities should succeed");
    ASSERT_TRUE(strlen(buf) > 0, "capabilities string should not be empty");

    PASS();
}

static void test_unified_to_native_null(void)
{
    TEST("unified_to_native with NULL msg returns error");
    int rc = openjiuwen_unified_to_native(NULL, NULL, 0);
    ASSERT_TRUE(rc < 0, "unified_to_native with NULL msg should fail");
    PASS();
}

static void test_native_to_unified_null(void)
{
    TEST("native_to_unified with NULL msg returns error");
    int rc = openjiuwen_native_to_unified(NULL, 0, NULL);
    ASSERT_TRUE(rc != 0, "native_to_unified with NULL msg should fail");
    PASS();
}

static void test_adapter_interface(void)
{
    TEST("adapter_interface is valid");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.name, "interface name should not be NULL");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.version, "interface version should not be NULL");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.initialize, "init should not be NULL");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.shutdown, "shutdown should not be NULL");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.send, "send should not be NULL");
    ASSERT_NOT_NULL(openjiuwen_adapter_interface.receive, "receive should not be NULL");
    PASS();
}

int main(void)
{
    AIRY_LOG_INFO("=== OpenJiuwen Adapter Unit Tests ===\n\n");

    test_config_default();
    test_get_default_config_null();
    test_adapter_create();
    test_adapter_create_null_config();
    test_version_constants();
    test_verify_connection_null();
    test_get_capabilities_null();
    test_get_capabilities();
    test_unified_to_native_null();
    test_native_to_unified_null();
    test_adapter_interface();

    AIRY_LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}