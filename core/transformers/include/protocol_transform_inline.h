/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_transform_inline.h
 * @brief 转换器共享内联机制：信封初始化、元数据透传、载荷透传。
 *
 * 这些助手只依赖机制核的中性类型，供核内通用转换器与厂商面转换器共用，
 * 避免逐文件重复实现（Unify Design SSoT）。本头不含任何厂商知识，
 * 属机制核职责（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#ifndef AIRY_RT_PROTOCOL_TRANSFORM_INLINE_H
#define AIRY_RT_PROTOCOL_TRANSFORM_INLINE_H

#include "protocol_transformers.h"

#include "airy_memory.h"
#include "types.h"

#include <string.h>

/* 清空目标消息并盖印 protocol/endpoint/direction 信封头。 */
static inline void envelope_init(unified_message_t *target, airy_protocol_type_t protocol,
                                 const char *endpoint, message_direction_t direction)
{
    AIRY_MEMSET(target, 0, sizeof(*target));
    target->protocol = protocol;
    AIRY_STRNCPY_TERM(target->endpoint, endpoint, sizeof(target->endpoint));
    target->direction = direction;
}

/* 从转换上下文透传 trace/session 元数据。 */
static inline void envelope_meta(unified_message_t *target, void *context)
{
    transform_context_t *ctx = (transform_context_t *)context;
    if (!ctx)
        return;
    if (ctx->trace_id[0])
        AIRY_STRNCPY_TERM(target->metadata.trace_id, ctx->trace_id,
                          sizeof(target->metadata.trace_id));
    if (ctx->session_id[0])
        AIRY_STRNCPY_TERM(target->metadata.session_id, ctx->session_id,
                          sizeof(target->metadata.session_id));
}

/* 透传源载荷；源无载荷时使用 fallback。 */
static inline void payload_pass(unified_message_t *target, const unified_message_t *source,
                                const char *fallback)
{
    if (source->payload) {
        target->payload_size = strlen((const char *)source->payload) + 1;
        target->payload = AIRY_STRDUP((const char *)source->payload);
    } else {
        target->payload = AIRY_STRDUP(fallback);
        target->payload_size = strlen((const char *)target->payload) + 1;
    }
}

#endif /* AIRY_RT_PROTOCOL_TRANSFORM_INLINE_H */
