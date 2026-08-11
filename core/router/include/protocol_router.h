/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_router.h
 * @brief Protocol Routing and Transformation Engine
 *
 * 协议路由与转换引擎，支持MCP/A2A/OpenAI API等协议的自适应路由和转换。
 * 提供智能协议路由、消息转换和协议兼容性处理。
 */

#ifndef AIRY_RT_PROTOCOL_ROUTER_H
#define AIRY_RT_PROTOCOL_ROUTER_H

#include "unified_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================ */

/* ============================================================================ */
/**
 * @brief 协议转换规则
 */
typedef struct {
    protocol_type_t source_protocol;
    protocol_type_t target_protocol;
    const char *source_endpoint;
    const char *target_endpoint;
    uint32_t priority;
    void *transformer_context;
} protocol_rule_t;

/**
 * @brief 消息转换函数类型
 * @param source 源消息
 * @param target 目标消息（输出）
 * @param context 转换器上下文
 * @return 转换结果：0成功，负数错误码
 */
typedef int (*message_transformer_t)(const unified_message_t *source, unified_message_t *target,
                                     void *context);

/**
 * @brief 路由决策函数类型
 * @param message 输入消息
 * @param rules 规则列表
 * @param rule_count 规则数量
 * @return 选择的规则索引，-1表示无匹配
 */
typedef int (*route_decision_func_t)(const unified_message_t *message, const protocol_rule_t *rules,
                                     size_t rule_count);

/**
 * @brief 路由引擎句柄
 */
typedef struct protocol_router_s *protocol_router_handle_t;

/* ============================================================================ */

/* ============================================================================ */
/**
 * @brief 创建路由引擎实例
 * @param default_protocol 默认协议（当无匹配规则时使用）
 * @return 路由引擎句柄，失败返回NULL
 */
protocol_router_handle_t protocol_router_create(protocol_type_t default_protocol);

/**
 * @brief 销毁路由引擎实例
 * @param router 路由引擎句柄
 */
void protocol_router_destroy(protocol_router_handle_t router);

/**
 * @brief 添加协议转换规则
 * @param router 路由引擎句柄
 * @param rule 转换规则
 * @param transformer 消息转换函数（可为NULL，使用默认转换）
 * @return 0成功，负数错误码
 */
int protocol_router_add_rule(protocol_router_handle_t router, const protocol_rule_t *rule,
                             message_transformer_t transformer);

/**
 * @brief 删除匹配源端点的协议转换规则
 *
 * 从路由表中删除所有 source_endpoint 与 source_endpoint_pattern
 * 精确相等的规则（真实删除并释放节点，非桩）。
 *
 * @param router 路由引擎句柄
 * @param source_endpoint_pattern 源端点模式（与 add_rule 时 rule->source_endpoint 一致）
 * @return 0 删除成功；AIRY_ERR_NOT_FOUND 无匹配规则；AIRY_EINVAL 参数无效
 */
int protocol_router_remove_rule(protocol_router_handle_t router,
                                const char *source_endpoint_pattern);

/**
 * @brief 路由消息
 * @param router 路由引擎句柄
 * @param message 输入消息
 * @param transformed 转换后的消息（输出）
 * @return 0成功，负数错误码
 *
 * @note 如果找不到匹配规则，使用默认协议直接传递消息（不转换）
 */
int protocol_router_route(protocol_router_handle_t router, const unified_message_t *message,
                          unified_message_t *transformed);

/**
 * @brief 批量路由消息
 * @param router 路由引擎句柄
 * @param messages 输入消息数组
 * @param count 消息数量
 * @param transformed 转换后的消息数组（输出）
 * @return 成功路由的消息数量，负数表示错误
 */
int protocol_router_route_batch(protocol_router_handle_t router, const unified_message_t *messages,
                                size_t count, unified_message_t *transformed);

/**
 * @brief 设置路由决策函数
 * @param router 路由引擎句柄
 * @param decision_func 路由决策函数
 * @return 0成功，负数错误码
 */
int protocol_router_set_decision_func(protocol_router_handle_t router,
                                      route_decision_func_t decision_func);

/**
 * @brief 获取路由统计信息
 * @param router 路由引擎句柄
 * @param stats_json 统计信息JSON字符串（输出，需要调用者释放）
 * @return 0成功，负数错误码
 */
int protocol_router_get_stats(protocol_router_handle_t router, char **stats_json);

/* ============================================================================ */

/* ============================================================================ */
/**
 * @brief JSON-RPC到MCP协议转换器
 * @param source 源消息（JSON-RPC）
 * @param target 目标消息（MCP）
 * @param context 转换器上下文
 * @return 0成功，负数错误码
 */
int protocol_transformer_jsonrpc_to_mcp(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
 * @brief MCP到JSON-RPC协议转换器
 * @param source 源消息（MCP）
 * @param target 目标消息（JSON-RPC）
 * @param context 转换器上下文
 * @return 0成功，负数错误码
 */
int protocol_transformer_mcp_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
 * @brief OpenAI API到JSON-RPC协议转换器
 * @param source 源消息（OpenAI API）
 * @param target 目标消息（JSON-RPC）
 * @param context 转换器上下文
 * @return 0成功，负数错误码
 */
int protocol_transformer_openai_to_jsonrpc(const unified_message_t *source,
                                           unified_message_t *target, void *context);

/**
 * @brief A2A到JSON-RPC协议转换器
 * @param source 源消息（A2A）
 * @param target 目标消息（JSON-RPC）
 * @param context 转换器上下文
 * @return 0成功，负数错误码
 */
int protocol_transformer_a2a_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context);

/**
 * @brief 默认消息转换器（直接复制，无格式转换）
 * @param source 源消息
 * @param target 目标消息
 * @param context 转换器上下文（忽略）
 * @return 0成功，负数错误码
 */
int protocol_transformer_default(const unified_message_t *source, unified_message_t *target,
                                 void *context);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_ROUTER_H */
