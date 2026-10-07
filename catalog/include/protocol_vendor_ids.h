/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/*
 * 厂商协议类型 ID 分配表（装配/策略数据）。
 *
 * 该数值是统一消息的线上契约（umsg_to_json 写入 "protocol":<id>，
 * unified_decode 读回），必须稳定映射到 AIRY_PROTOCOL_VENDOR_BASE 起的连续
 * 区间，与历史枚举取值一一对应。机制核不携带厂商名，故本表置于编制区
 * （protocols/catalog/），由装配层与厂商面适配器共用。
 */

/* @owner: team-B */
#ifndef AIRY_RT_PROTOCOL_VENDOR_IDS_H
#define AIRY_RT_PROTOCOL_VENDOR_IDS_H

#include "unified_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIRY_PROTOCOL_OPENAI ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 0))
#define AIRY_PROTOCOL_OPENJIUWEN ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 1))
#define AIRY_PROTOCOL_CLAUDE ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 2))
#define AIRY_PROTOCOL_CHINA_ECO ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 3))
#define AIRY_PROTOCOL_AGNTCY ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 4))
#define AIRY_PROTOCOL_OPENCLAW ((protocol_type_t)(AIRY_PROTOCOL_VENDOR_BASE + 5))

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_VENDOR_IDS_H */
