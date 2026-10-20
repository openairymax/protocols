// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_protocol_detect.c
 * @brief Protocol Body Detection Mechanism Unit Tests
 */
// @owner: team-B

#include "protocol_detect.h"

#include "unified_protocol.h"

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                  \
    do {                            \
        printf("  TEST: %s ... ", name); \
    } while (0)
#define PASS()         \
    do {               \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)
#define FAIL(msg)                   \
    do {                            \
        printf("FAIL: %s\n", msg);  \
        tests_failed++;             \
    } while (0)
#define ASSERT_TRUE(cond, msg) \
    do {                       \
        if (!(cond)) {         \
            FAIL(msg);         \
            return;            \
        }                      \
    } while (0)

static void test_invalid_input(void)
{
    TEST("null/empty/malformed input yields unknown");

    ASSERT_TRUE(proto_detect_body(NULL, 16) == AIRY_PROTOCOL_COUNT, "null body must be unknown");
    ASSERT_TRUE(proto_detect_body("{}", 0) == AIRY_PROTOCOL_COUNT, "empty body must be unknown");
    ASSERT_TRUE(proto_detect_body("not json", 8) == AIRY_PROTOCOL_COUNT,
                "non-JSON body must be unknown");
    ASSERT_TRUE(proto_detect_body("{\"broken\":", 10) == AIRY_PROTOCOL_COUNT,
                "truncated JSON must be unknown");

    PASS();
}

static void test_jsonrpc_envelope(void)
{
    TEST("plain JSON-RPC 2.0 envelope");

    const char *body = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}";
    ASSERT_TRUE(proto_detect_body(body, strlen(body)) == AIRY_PROTOCOL_JSON_RPC,
                "plain method must be JSON-RPC");

    PASS();
}

static void test_mcp_method_namespaces(void)
{
    TEST("MCP detection via namespaced methods");

    const char *tools = "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":2}";
    const char *resources = "{\"jsonrpc\":\"2.0\",\"method\":\"resources/list\",\"id\":3}";
    const char *prompts = "{\"jsonrpc\":\"2.0\",\"method\":\"prompts/get\",\"id\":4}";

    ASSERT_TRUE(proto_detect_body(tools, strlen(tools)) == AIRY_PROTOCOL_MCP,
                "tools/call must be MCP");
    ASSERT_TRUE(proto_detect_body(resources, strlen(resources)) == AIRY_PROTOCOL_MCP,
                "resources/list must be MCP");
    ASSERT_TRUE(proto_detect_body(prompts, strlen(prompts)) == AIRY_PROTOCOL_MCP,
                "prompts/get must be MCP");

    PASS();
}

static void test_a2a_inside_jsonrpc(void)
{
    TEST("A2A detection inside JSON-RPC envelope");

    const char *send = "{\"jsonrpc\":\"2.0\",\"method\":\"tasks/send\",\"id\":5}";
    const char *get = "{\"jsonrpc\":\"2.0\",\"method\":\"task/get\",\"id\":6}";
    const char *msg = "{\"jsonrpc\":\"2.0\",\"method\":\"message/send\",\"id\":7}";

    ASSERT_TRUE(proto_detect_body(send, strlen(send)) == AIRY_PROTOCOL_A2A,
                "tasks/send must be A2A");
    ASSERT_TRUE(proto_detect_body(get, strlen(get)) == AIRY_PROTOCOL_A2A,
                "task/get must be A2A");
    ASSERT_TRUE(proto_detect_body(msg, strlen(msg)) == AIRY_PROTOCOL_A2A,
                "message/send must be A2A");

    PASS();
}

static void test_llm_shape(void)
{
    TEST("LLM API detection via model+messages and legacy prompt");

    const char *chat =
        "{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}";
    const char *legacy = "{\"model\":\"m\",\"prompt\":\"hi\"}";

    ASSERT_TRUE(proto_detect_body(chat, strlen(chat)) == AIRY_PROTOCOL_LLM,
                "model+messages must be LLM API");
    ASSERT_TRUE(proto_detect_body(legacy, strlen(legacy)) == AIRY_PROTOCOL_LLM,
                "model+prompt must be LLM API");

    PASS();
}

static void test_non_chat_model_bodies_stay_unknown(void)
{
    TEST("embeddings-style body without messages stays unknown");

    const char *embed = "{\"model\":\"test-embed\",\"input\":[\"hello\"]}";
    ASSERT_TRUE(proto_detect_body(embed, strlen(embed)) == AIRY_PROTOCOL_COUNT,
                "model+input alone must not be LLM API");

    PASS();
}

static void test_a2a_structural_fallbacks(void)
{
    TEST("A2A structural and keyword fallbacks");

    const char *structured = "{\"agent_id\":\"a1\",\"task_id\":\"t1\"}";
    const char *message = "{\"agent_id\":\"a1\",\"message\":{}}";
    const char *card = "{\"agentCard\":{\"name\":\"n\"}}";
    const char *task = "{\"task\":{\"id\":\"t1\"}}";
    const char *a2a = "{\"a2a\":{\"version\":\"0.3\"}}";

    ASSERT_TRUE(proto_detect_body(structured, strlen(structured)) == AIRY_PROTOCOL_A2A,
                "agent_id+task_id must be A2A");
    ASSERT_TRUE(proto_detect_body(message, strlen(message)) == AIRY_PROTOCOL_A2A,
                "agent_id+message must be A2A");
    ASSERT_TRUE(proto_detect_body(card, strlen(card)) == AIRY_PROTOCOL_A2A,
                "top-level agentCard must be A2A");
    ASSERT_TRUE(proto_detect_body(task, strlen(task)) == AIRY_PROTOCOL_A2A,
                "top-level task must be A2A");
    ASSERT_TRUE(proto_detect_body(a2a, strlen(a2a)) == AIRY_PROTOCOL_A2A,
                "top-level a2a must be A2A");

    PASS();
}

static void test_legacy_mcp_key_is_not_mcp(void)
{
    TEST("top-level mcp key no longer implies MCP");

    const char *body = "{\"mcp\":{}}";
    ASSERT_TRUE(proto_detect_body(body, strlen(body)) == AIRY_PROTOCOL_COUNT,
                "bare mcp key without JSON-RPC envelope must stay unknown");

    PASS();
}

int main(void)
{
    printf("=== Protocol Body Detection Tests ===\n");

    test_invalid_input();
    test_jsonrpc_envelope();
    test_mcp_method_namespaces();
    test_a2a_inside_jsonrpc();
    test_llm_shape();
    test_non_chat_model_bodies_stay_unknown();
    test_a2a_structural_fallbacks();
    test_legacy_mcp_key_is_not_mcp();

    printf("\n=== %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
