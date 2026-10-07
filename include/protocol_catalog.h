/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/*
 * AgentRT 协议目录端口（L2 契约）。
 *
 * 机制核只定义取目录与取转换表的端口，不携带任何具体协议集合或厂商转换：
 * 装配层提供 proto_catalog_defs() 的内置协议描述表，供注册表批量登记；
 * 并提供 proto_catalog_transforms() 的厂商转换描述表，供自动转换器在核内
 * 标准表未命中时回退查询。这是机制与策略分离的注入接缝
 * （见 0.1.19 架构方案 §4.7/§5.1）。
 */

/* @owner: team-B */
#ifndef AIRY_RT_PROTOCOL_CATALOG_H
#define AIRY_RT_PROTOCOL_CATALOG_H

#include "protocol_registry.h"
#include "protocol_transformers.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 取得内置协议描述表。
 *
 * 由装配层实现。返回的表在进程生命周期内保持有效；适配器绑定在首次调用
 * 时解析完成。out_count 可传 NULL。
 */
const proto_builtin_def_t *proto_catalog_defs(size_t *out_count);

/**
 * @brief 取得厂商协议转换描述表。
 *
 * 由装配层实现，表中的 (from_proto, to_proto) 到转换函数的映射在进程生命
 * 周期内保持有效。返回的表以 from_proto == NULL 的哨兵项结束。out_count
 * 可传 NULL。核内标准转换不经此端口，本表只承载厂商面转换。
 */
const proto_transform_def_t *proto_catalog_transforms(size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_CATALOG_H */
