// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_catalog_transforms.c
 * @brief 厂商转换端口实现（装配数据）。
 *
 * 机制核（core/transformers）仅内置开放标准（JSON-RPC/MCP/A2A）转换器，
 * 其自动转换器在标准表未命中时经端口 proto_xform_defs() 查询厂商
 * 转换。厂商转换器已按机制与策略分离原则迁出机制核，改由生态/装配面注入；
 * 本端口当前返回空表（以 from_proto == NULL 哨兵结束），未命中即透传，
 * 语义与既有一致（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#include "protocol_catalog.h"

#include "types.h"

static const proto_transform_def_t g_catalog_transforms[] = {
    {NULL, NULL, NULL},
};

const proto_transform_def_t *proto_xform_defs(size_t *out_count)
{
    if (out_count)
        *out_count = AIRY_ARRAY_SIZE(g_catalog_transforms) - 1;
    return g_catalog_transforms;
}
