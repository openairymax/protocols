// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_std_providers.c
 * @brief 标准协议适配器提供商表（装配数据）。
 *
 * 标准层拥有随核发布的开放标准适配器（A2A/MCP），在此声明各协议类型到其
 * 适配器访问器的绑定，经端口 proto_std_providers() 提供给装配层。装配
 * 数据因此不再直接引用具体适配器符号，机制与策略分离的接缝收敛于此
 * （见 0.1.19 架构方案 §4.7/§5.1）。
 *
 * 未随核编译的协议（如 MCP 关闭时）不登记，注册表仍通告其类型但不冒充实现。
 */

#include "protocol_catalog.h"

#include "a2a_v03_adapter.h"
#include "types.h"

#if defined(AIRY_HAS_MCP)
#include "mcp_v1_adapter.h"
#endif

static const proto_adapter_provider_t g_std_providers[] = {
    {PROTO_A2A, a2a_v03_get_adapter},
#if defined(AIRY_HAS_MCP)
    {PROTO_MCP, mcp_v1_get_adapter},
#endif
};

const proto_adapter_provider_t *proto_std_providers(size_t *out_count)
{
    if (out_count)
        *out_count = AIRY_ARRAY_SIZE(g_std_providers);
    return g_std_providers;
}
