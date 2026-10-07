// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_protocol_registry.c
 * @brief Protocol Registry Mechanism Unit Tests
 */
// @owner: team-B

#include "protocol_registry.h"

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

static void test_get_is_singleton(void)
{
    TEST("get returns one process-wide instance");

    protocol_registry_t *first = proto_registry_get();
    protocol_registry_t *second = proto_registry_get();

    ASSERT_NOT_NULL(first, "get should return a valid instance");
    ASSERT_TRUE(first == second, "get must be idempotent");

    PASS();
}

static void test_create_does_not_pollute_singleton(void)
{
    TEST("create allocates a distinct instance without touching the singleton");

    protocol_registry_t *singleton = proto_registry_get();
    ASSERT_NOT_NULL(singleton, "singleton must exist");

    protocol_registry_t *separate = proto_registry_create();
    ASSERT_NOT_NULL(separate, "create should return a new instance");
    ASSERT_TRUE(separate != singleton, "create must allocate a distinct instance");
    ASSERT_TRUE(proto_registry_get() == singleton, "create must not redirect the singleton");

    proto_registry_destroy(separate);
    PASS();
}

static void test_register_preserves_injection(void)
{
    TEST("register stores the adapter and context through the seam");

    protocol_registry_t *registry = proto_registry_get();

    static const protocol_adapter_t adapter = {0};
    static int context_marker = 0;

    int rc = proto_registry_register(registry, "unit_seam", "1.0", "unit probe",
                                     PROTO_CAT_CUSTOM, AIRY_PROTOCOL_JSON_RPC, 0, &adapter,
                                     &context_marker);
    ASSERT_TRUE(rc == 0, "register should succeed");

    proto_registry_entry_t *entry = proto_registry_find(registry, "unit_seam");
    ASSERT_NOT_NULL(entry, "entry should be findable");
    ASSERT_TRUE(entry->adapter == &adapter, "adapter pointer must be preserved");
    ASSERT_TRUE(entry->context == &context_marker, "context pointer must be preserved");

    ASSERT_TRUE(proto_registry_unregister(registry, "unit_seam") == 0, "unregister should succeed");
    PASS();
}

static void test_destroy_resets_singleton(void)
{
    TEST("destroy resets the singleton to a fresh empty instance");

    protocol_registry_t *registry = proto_registry_get();
    int rc = proto_registry_register(registry, "reset_probe", "1.0", NULL, PROTO_CAT_CUSTOM,
                                     AIRY_PROTOCOL_JSON_RPC, 0, NULL, NULL);
    ASSERT_TRUE(rc == 0, "probe registration should succeed");

    proto_registry_destroy(registry);

    protocol_registry_t *fresh = proto_registry_get();
    ASSERT_NOT_NULL(fresh, "get after destroy should recreate the singleton");
    ASSERT_TRUE(proto_registry_find(fresh, "reset_probe") == NULL, "fresh instance must be empty");

    PASS();
}

static void test_builtins_preserve_injection(void)
{
    TEST("builtin defs carry their adapter and context into the entries");

    protocol_registry_t *registry = proto_registry_create();
    ASSERT_NOT_NULL(registry, "registry should be created");

    static const protocol_adapter_t adapter = {0};
    static int context_marker = 0;
    static const proto_builtin_def_t defs[] = {
        {"bind_probe", "1.0", "bind probe", PROTO_CAT_CUSTOM, AIRY_PROTOCOL_JSON_RPC, 0, &adapter,
         &context_marker},
    };

    int count = proto_reg_builtins(registry, defs, sizeof(defs) / sizeof(defs[0]));
    ASSERT_TRUE(count == 1, "one builtin should register");

    proto_registry_entry_t *entry = proto_registry_find(registry, "bind_probe");
    ASSERT_NOT_NULL(entry, "builtin entry should be findable");
    ASSERT_TRUE(entry->adapter == &adapter, "builtin adapter pointer must be preserved");
    ASSERT_TRUE(entry->context == &context_marker, "builtin context pointer must be preserved");

    proto_registry_destroy(registry);
    PASS();
}

int main(void)
{
    AIRY_LOG_INFO("=== Protocol Registry Mechanism Unit Tests ===\n\n");

    test_get_is_singleton();
    test_create_does_not_pollute_singleton();
    test_register_preserves_injection();
    test_builtins_preserve_injection();
    test_destroy_resets_singleton();

    AIRY_LOG_INFO("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
