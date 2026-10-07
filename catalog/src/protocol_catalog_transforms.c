// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_catalog_transforms.c
 * @brief 默认厂商转换表实现（装配数据）。
 *
 * 机制核（core/transformers）仅内置开放标准（JSON-RPC/MCP/A2A）转换器；厂商面
 * 转换器位于各自集成目录，由装配层在此聚合，经端口 proto_catalog_transforms()
 * 注入机制核的自动转换器，遵循机制与策略分离
 * （见 0.1.19 架构方案 §4.7/§5.1）。
 */

#include "protocol_catalog.h"

#include "types.h"

#include "openai_transform.h"
#include "openjiuwen_transform.h"

static const proto_transform_def_t g_catalog_transforms[] = {
    {"jsonrpc", "openai", oai_chat_req},
    {"openai", "jsonrpc", oai_chat_resp},
    {"jsonrpc", "openjiuwen", ojw_req},
    {"openjiuwen", "jsonrpc", ojw_resp},
    {NULL, NULL, NULL},
};

const proto_transform_def_t *proto_catalog_transforms(size_t *out_count)
{
    if (out_count)
        *out_count = AIRY_ARRAY_SIZE(g_catalog_transforms) - 1;
    return g_catalog_transforms;
}
