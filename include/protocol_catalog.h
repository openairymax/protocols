/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/*
 * AgentRT 协议目录端口（L2 契约）。
 *
 * 机制核只定义取目录、取转换表与取标准适配器提供商表的端口，不携带任何
 * 具体协议集合、厂商转换或适配器绑定：装配层提供 proto_catalog_defs() 的
 * 内置协议描述表，供注册表批量登记；并提供 proto_xform_defs() 的
 * 厂商转换描述表，供自动转换器在核内标准表未命中时回退查询；标准层提供
 * proto_std_providers() 的适配器提供商表，把协议类型映射到随核发布的
 * 开放标准适配器。这是机制与策略分离的注入接缝
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
const proto_transform_def_t *proto_xform_defs(size_t *out_count);

/**
 * @brief 协议类型到适配器访问器的绑定项。
 *
 * type 为统一协议类型；get_adapter 返回该类型对应的适配器，可为 NULL 表示
 * 仅通告协议而不冒充实现。
 */
typedef struct {
    proto_type_t type;
    const protocol_adapter_t *(*get_adapter)(void);
} proto_adapter_provider_t;

/**
 * @brief 取得标准适配器提供商表。
 *
 * 由标准层实现，把随核发布的开放标准协议类型映射到各自的适配器访问器。
 * 装配层据此完成类型到适配器的绑定，装配数据不再硬编码具体适配器符号。
 * 返回的表在进程生命周期内保持有效。out_count 可传 NULL。
 */
const proto_adapter_provider_t *proto_std_providers(size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_PROTOCOL_CATALOG_H */
