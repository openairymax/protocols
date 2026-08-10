// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
// @owner: team-B
/**
 * @file unified_protocol.c
 * @brief Unified Protocol Implementation
 *
 * 统一协议接口实现，提供协议无关的消息处理和路由功能。
 */

#include "unified_protocol.h"

#include "airy_memory.h"
#include "platform.h"
#include "safe_string_utils.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "error.h"

typedef struct {
    airy_protocol_type_t type;
    const char *name;
    int (*init)(void *context);
    int (*encode)(void *context, const void *msg, void **out_data, size_t *out_size);
    int (*decode)(void *context, const void *data, size_t size, void *out_msg);
    void (*destroy)(void *context);
    int (*connect)(void *context, const char *endpoint);
    int (*send)(void *context, const void *data, size_t size);
    int (*receive)(void *context, void **data, size_t *size, uint32_t timeout_ms);
} adapter_impl_t;

// ============================================================================
// 内部数据结构
// ============================================================================

typedef struct protocol_adapter_node_s {
    protocol_adapter_t *adapter;
    void *context;
    struct protocol_adapter_node_s *next;
} protocol_adapter_node_t;

struct protocol_stack_s {
    protocol_stack_config_t config;
    protocol_adapter_node_t *adapters;
    size_t adapter_count;
    bool initialized;

    // 回调函数
    void (*message_callback)(const unified_message_t *message, void *user_data);
    void *callback_user_data;

    // 统计信息
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
};

// ============================================================================
// 静态函数声明
// ============================================================================

static protocol_adapter_node_t *find_adapter_node(protocol_stack_handle_t handle,
                                                  protocol_type_t type);
static int validate_message(const unified_message_t *message);
static uint64_t get_current_timestamp(void);

// ============================================================================
// 核心API实现
// ============================================================================

protocol_stack_handle_t protocol_stack_create(const protocol_stack_config_t *config)
{
    if (!config) {
        return NULL;
    }

    struct protocol_stack_s *stack =
        (struct protocol_stack_s *)AIRY_CALLOC(1, sizeof(struct protocol_stack_s));
    if (!stack) {
        return NULL;
    }

    // 复制配置
    stack->config = *config;
    {
        size_t name_len = strlen(config->name) + 1;
        char *name_copy = (char *)AIRY_MALLOC(name_len);
        if (name_copy) {
            safe_strcpy(name_copy, config->name, name_len);
            AIRY_STRNCPY_TERM(stack->config.name, name_copy, sizeof(stack->config.name));
            stack->config.name[sizeof(stack->config.name) - 1] = '\0';
            AIRY_FREE(name_copy);
        }
    }

    stack->adapters = NULL;
    stack->adapter_count = 0;
    stack->initialized = true;

    stack->messages_sent = 0;
    stack->messages_received = 0;
    stack->bytes_sent = 0;
    stack->bytes_received = 0;

    return stack;
}

void protocol_stack_destroy(protocol_stack_handle_t handle)
{
    if (!handle)
        return;

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;

    // 销毁所有适配器
    protocol_adapter_node_t *node = stack->adapters;
    while (node) {
        protocol_adapter_node_t *next = node->next;
        if (node->adapter && node->adapter->destroy) {
            node->adapter->destroy(node->context);
            AIRY_FREE(node->adapter);
        }
        AIRY_FREE(node);
        node = next;
    }

    if (stack->config.custom_config) {
        AIRY_FREE(stack->config.custom_config);
    }

    AIRY_FREE(stack);
}

int protocol_stack_register_adapter(protocol_stack_handle_t handle, protocol_adapter_t adapter)
{
    if (!handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "protocol_stack_register_adapter: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;

    // 检查是否已存在相同类型的适配器
    protocol_adapter_node_t *existing = find_adapter_node(handle, adapter.type);
    if (existing) {
        // 已存在，替换
        if (existing->adapter && existing->adapter->destroy) {
            existing->adapter->destroy(existing->context);
        }
        *existing->adapter = adapter;
        if (adapter.init) {
            adapter.init(existing->context);
        }
        return 0;
    }

    // 创建新节点
    protocol_adapter_node_t *node =
        (protocol_adapter_node_t *)AIRY_MALLOC(sizeof(protocol_adapter_node_t));
    if (!node) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "AIRY_MALLOC: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    node->adapter = (protocol_adapter_t *)AIRY_MALLOC(sizeof(protocol_adapter_t));
    if (!node->adapter) {
        AIRY_FREE(node);
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__, "AIRY_MALLOC: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    *node->adapter = adapter;
    node->context = NULL;

    // 初始化适配器
    if (adapter.init) {
        int result = adapter.init(node->context);
        if (result != 0) {
            AIRY_FREE(node->adapter);
            AIRY_FREE(node);
            return result;
        }
    }

    // 添加到链表
    node->next = stack->adapters;
    stack->adapters = node;
    stack->adapter_count++;

    return 0;
}

int protocol_stack_send(protocol_stack_handle_t handle, const unified_message_t *message)
{
    if (!handle || !message) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "protocol_stack_send: IO error");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;

    // 验证消息
    int valid = validate_message(message);
    if (valid != 0) {
        return valid;
    }

    // 查找对应的适配器
    protocol_adapter_node_t *adapter_node = find_adapter_node(handle, message->protocol);
    if (!adapter_node || !adapter_node->adapter) {
        airy_err_push_ex(AIRY_ERR_NOT_FOUND, __FILE__, __LINE__, __func__, "route_message: adapter not found");
        return AIRY_ERR_NOT_FOUND;
    }

    protocol_adapter_t *adapter = adapter_node->adapter;

    // 编码消息
    void *encoded_data = NULL;
    size_t encoded_size = 0;
    if (adapter->encode) {
        int result = adapter->encode(adapter_node->context, message, &encoded_data, &encoded_size);
        if (result != 0) {
            return result;
        }
    } else {
        // 如果没有编码器，直接使用原始数据
        encoded_data = (void *)message->payload;
        encoded_size = message->payload_size;
    }

    if (adapter->connect) {
        int conn_result = adapter->connect(adapter_node->context, message->endpoint);
        if (conn_result != 0 && conn_result != -2) {
            if (encoded_data != message->payload && encoded_data) {
                AIRY_FREE(encoded_data);
            }
            return conn_result;
        }
    }

    if (adapter->send) {
        int send_result = adapter->send(adapter_node->context, encoded_data, encoded_size);
        if (send_result != 0) {
            if (encoded_data != message->payload && encoded_data) {
                AIRY_FREE(encoded_data);
            }
            return send_result;
        }
    } else if (stack->message_callback) {
        stack->message_callback(message, stack->callback_user_data);
    }

    stack->messages_sent++;
    stack->bytes_sent += encoded_size;

    if (encoded_data != message->payload && encoded_data) {
        AIRY_FREE(encoded_data);
    }

    return 0;
}

int protocol_stack_receive(protocol_stack_handle_t handle, unified_message_t *message,
                           uint32_t timeout_ms)
{
    if (!handle || !message) {
        airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "protocol_stack_receive: timeout");
        return AIRY_ERR_TIMEOUT;
    }

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;

    AIRY_MEMSET(message, 0, sizeof(unified_message_t));

    protocol_adapter_node_t *adapter_node = stack->adapters;
    while (adapter_node) {
        if (adapter_node->adapter && adapter_node->adapter->receive) {
            void *decoded_data = NULL;
            size_t decoded_size = 0;

            int result = adapter_node->adapter->receive(adapter_node->context, &decoded_data,
                                                        &decoded_size, timeout_ms);

            if (result == 0 && decoded_data && decoded_size > 0) {
                if (adapter_node->adapter->decode) {
                    int dec_result = adapter_node->adapter->decode(
                        adapter_node->context, decoded_data, decoded_size, message);
                    if (dec_result != 0) {
                        AIRY_FREE(decoded_data);
                        adapter_node = adapter_node->next;
                        continue;
                    }
                } else {
                    message->payload = decoded_data;
                    message->payload_size = decoded_size;
                }

                message->protocol = adapter_node->adapter->type;
                message->direction = DIRECTION_RESPONSE;
                message->timestamp = get_current_timestamp();
                message->message_id = 0;

                stack->messages_received++;
                stack->bytes_received += decoded_size;

                if (stack->message_callback) {
                    stack->message_callback(message, stack->callback_user_data);
                }

                return 0;
            }

            if (decoded_data)
                AIRY_FREE(decoded_data);
        }
        adapter_node = adapter_node->next;
    }

    if (timeout_ms > 0) {
        struct timespec ts = {.tv_sec = timeout_ms / 1000,
                              .tv_nsec = (timeout_ms % 1000) * 1000000LL};
        nanosleep(&ts, NULL);
    }

    airy_err_push_ex(AIRY_ERR_TIMEOUT, __FILE__, __LINE__, __func__, "nanosleep: timeout");
    return AIRY_ERR_TIMEOUT;
}

int protocol_stack_set_callback(protocol_stack_handle_t handle,
                                void (*callback)(const unified_message_t *message, void *user_data),
                                void *user_data)
{
    if (!handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "protocol_stack_set_callback: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;
    stack->message_callback = callback;
    stack->callback_user_data = user_data;

    return 0;
}

int protocol_stack_get_stats(protocol_stack_handle_t handle, void *stats)
{
    if (!handle || !stats) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "protocol_stack_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;

    typedef struct {
        uint64_t messages_sent;
        uint64_t messages_received;
        uint64_t bytes_sent;
        uint64_t bytes_received;
        size_t adapter_count;
        bool initialized;
    } protocol_stats_t;

    protocol_stats_t *out = (protocol_stats_t *)stats;
    AIRY_MEMSET(out, 0, sizeof(*out));

    out->messages_sent = stack->messages_sent;
    out->messages_received = stack->messages_received;
    out->bytes_sent = stack->bytes_sent;
    out->bytes_received = stack->bytes_received;
    out->adapter_count = stack->adapter_count;
    out->initialized = stack->initialized;

    return 0;
}

// ============================================================================
// 工具函数实现
// ============================================================================

unified_message_t unified_message_create(protocol_type_t protocol, message_direction_t direction,
                                         const char *endpoint, const void *payload,
                                         size_t payload_size)
{
    unified_message_t message;
    AIRY_MEMSET(&message, 0, sizeof(message));

    static uint64_t next_message_id = 1;

    message.message_id = next_message_id++;
    message.protocol = protocol;
    message.direction = direction;
    if (endpoint) {
        AIRY_STRNCPY_TERM(message.endpoint, endpoint, sizeof(message.endpoint));
    }
    message.payload = (void *)payload;
    message.payload_size = payload_size;
    message.timestamp = get_current_timestamp();

    return message;
}

void unified_message_destroy(unified_message_t *message)
{
    if (!message)
        return;

    // 注意：这里不释放payload，由调用者管理
    AIRY_MEMSET(message, 0, sizeof(unified_message_t));
}

const char *protocol_type_to_string(protocol_type_t type)
{
    /* P0-15 修复: 名称数组对齐 airy_protocol_type_t 枚举（9 个值 0-8）。
     * 历史 bug：数组只有 8 项旧传输层协议名（HTTP/WebSocket/gRPC/...），
     * 与当前 9 项应用层枚举完全不匹配。调用 protocol_type_to_string(MCP) 会
     * 错误返回 "WebSocket"（索引 1 = 旧 WebSocket 位置）。 */
    static const char *names[] = {
        "JSON-RPC",   /* 0: AIRY_PROTOCOL_JSON_RPC */
        "MCP",        /* 1: AIRY_PROTOCOL_MCP */
        "A2A",        /* 2: AIRY_PROTOCOL_A2A */
        "OpenAI",     /* 3: AIRY_PROTOCOL_OPENAI */
        "OpenJiuwen", /* 4: AIRY_PROTOCOL_OPENJIUWEN */
        "Claude",     /* 5: AIRY_PROTOCOL_CLAUDE */
        "ChinaEco",   /* 6: AIRY_PROTOCOL_CHINA_ECO */
        "AGNTCY",     /* 7: AIRY_PROTOCOL_AGNTCY */
        "OpenClaw"    /* 8: AIRY_PROTOCOL_OPENCLAW */
    };

    if (type < 0 || type >= AIRY_PROTOCOL_COUNT) {
        return "Unknown";
    }

    return names[type];
}

protocol_type_t protocol_type_from_string(const char *str)
{
    if (!str)
        return PROTOCOL_CUSTOM;

    /* P0-15 修复: 新增全部 9 种应用层协议名的字符串→枚举映射。
     * 历史 bug：from_string 只识别旧传输层协议名（http/websocket/grpc/...），
     * 调用 protocol_type_from_string("mcp") 返回 PROTOCOL_CUSTOM 而非 PROTO_MCP。 */
    if (strcasecmp(str, "json-rpc") == 0 || strcasecmp(str, "http") == 0)
        return AIRY_PROTOCOL_JSON_RPC;
    if (strcasecmp(str, "mcp") == 0 || strcasecmp(str, "mcp_v1") == 0)
        return AIRY_PROTOCOL_MCP;
    if (strcasecmp(str, "a2a") == 0 || strcasecmp(str, "a2a_v03") == 0)
        return AIRY_PROTOCOL_A2A;
    if (strcasecmp(str, "openai") == 0)
        return AIRY_PROTOCOL_OPENAI;
    if (strcasecmp(str, "openjiuwen") == 0)
        return AIRY_PROTOCOL_OPENJIUWEN;
    if (strcasecmp(str, "claude") == 0)
        return AIRY_PROTOCOL_CLAUDE;
    if (strcasecmp(str, "china-eco") == 0 || strcasecmp(str, "china_eco") == 0)
        return AIRY_PROTOCOL_CHINA_ECO;
    if (strcasecmp(str, "agntcy") == 0 || strcasecmp(str, "acp") == 0)
        return AIRY_PROTOCOL_AGNTCY;
    if (strcasecmp(str, "openclaw") == 0)
        return AIRY_PROTOCOL_OPENCLAW;

    return PROTOCOL_CUSTOM;
}

// ============================================================================
// 静态函数实现
// ============================================================================

static protocol_adapter_node_t *find_adapter_node(protocol_stack_handle_t handle,
                                                  protocol_type_t type)
{
    struct protocol_stack_s *stack = (struct protocol_stack_s *)handle;
    protocol_adapter_node_t *node = stack->adapters;

    while (node) {
        if (node->adapter && ((adapter_impl_t *)node->adapter)->type == type) {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

static int validate_message(const unified_message_t *message)
{
    if (!message)
        {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "validate_message: failed");
        return AIRY_ERR_UNKNOWN;
        }

    if (message->protocol < AIRY_PROTOCOL_JSON_RPC || message->protocol >= AIRY_PROTOCOL_COUNT) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }

    if (message->direction < DIRECTION_REQUEST || message->direction > DIRECTION_NOTIFICATION) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }

    if (message->endpoint[0] && strlen(message->endpoint) > 1024) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "if: failed");
        return AIRY_ERR_UNKNOWN;
    }

    return 0;
}

static uint64_t get_current_timestamp(void)
{
    return airy_time_ns();
}