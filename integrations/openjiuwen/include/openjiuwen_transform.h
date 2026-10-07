/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file openjiuwen_transform.h
 * @brief OpenJiuwen 二进制帧转换器（厂商面）。
 *
 * 承载 JSON-RPC 2.0 与 OpenJiuwen 私有二进制帧之间的双向转换。这些转换器属
 * 策略面，位于厂商集成目录内，经装配层 proto_catalog_transforms() 端口注入
 * 机制核的自动转换器（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#ifndef AIRY_RT_OPENJIUWEN_TRANSFORM_H
#define AIRY_RT_OPENJIUWEN_TRANSFORM_H

#include "protocol_transformers.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSON-RPC 请求 -> OpenJiuwen 二进制帧
 */
int ojw_req(const unified_message_t *source, unified_message_t *target, void *context);

/**
 * @brief OpenJiuwen 二进制帧 -> JSON-RPC 响应
 */
int ojw_resp(const unified_message_t *source, unified_message_t *target, void *context);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_OPENJIUWEN_TRANSFORM_H */
