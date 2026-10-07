/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file openai_transform.h
 * @brief OpenAI 面消息转换器（厂商面）。
 *
 * 承载 JSON-RPC 2.0 与 OpenAI 兼容 API 之间的双向转换。这些转换器属策略面，
 * 位于厂商集成目录内，经装配层 proto_catalog_transforms() 端口注入机制核的
 * 自动转换器（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#ifndef AIRY_RT_OPENAI_TRANSFORM_H
#define AIRY_RT_OPENAI_TRANSFORM_H

#include "protocol_transformers.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSON-RPC llm.complete 请求 -> OpenAI /v1/chat/completions 请求
 */
int oai_chat_req(const unified_message_t *source, unified_message_t *target, void *context);

/**
 * @brief OpenAI chat completions 响应 -> JSON-RPC 响应
 */
int oai_chat_resp(const unified_message_t *source, unified_message_t *target, void *context);

/**
 * @brief OpenAI 流式分片 -> JSON-RPC 通知
 */
int oai_stream_resp(const unified_message_t *source, unified_message_t *target, void *context);

/**
 * @brief JSON-RPC embedding 请求 -> OpenAI /v1/embeddings 请求
 */
int oai_embed_req(const unified_message_t *source, unified_message_t *target, void *context);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_OPENAI_TRANSFORM_H */
