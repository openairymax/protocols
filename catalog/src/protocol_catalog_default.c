// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_catalog_default.c
 * @brief 默认协议目录实现（装配数据）。
 *
 * 机制核（protocols/include、protocols/src、protocols/core）不携带任何厂商
 * 知识；具体协议集合在此以纯数据提供，经端口 proto_catalog_defs() 注入注册
 * 表。类型到适配器的绑定不在本文件硬编码，而由标准层经端口
 * proto_std_providers() 提供，遵循机制与策略分离
 * （见 0.1.19 架构方案 §4.7/§5.1）。厂商类型 ID 见 protocol_vendor_ids.h，
 * 其数值为统一消息线上契约，保持稳定。
 */

#include "protocol_catalog.h"

#include "protocol_vendor_ids.h"

#include "types.h"

/* 仅解析标准层登记为提供商的标准适配器访问器；未登记条目保持 NULL，注册表
 * 仍对外通告协议，但不冒充实现（无桩、无演示实现）。 */
static const protocol_adapter_t *catalog_adapter_of(proto_type_t type)
{
    size_t count = 0;
    const proto_adapter_provider_t *providers = proto_std_providers(&count);

    for (size_t i = 0; i < count; i++) {
        if (providers[i].type == type && providers[i].get_adapter)
            return providers[i].get_adapter();
    }
    return NULL;
}

static proto_builtin_def_t g_catalog_defs[] = {
    {"JSON-RPC", "2.0", "原生JSON-RPC 2.0协议适配器", PROTO_CAT_CORE, PROTO_JSONRPC,
     PROTO_CAP_STREAMING | PROTO_CAP_BATCH, NULL, NULL},
    {"MCP", "1.0", "Model Context Protocol v1.0", PROTO_CAT_STANDARD, PROTO_MCP,
     PROTO_CAP_TOOL_CALLING | PROTO_CAP_STREAMING | PROTO_CAP_RESOURCE_ACCESS, NULL, NULL},
    {"A2A", "0.3", "Agent-to-Agent Protocol v0.3", PROTO_CAT_STANDARD, PROTO_A2A,
     PROTO_CAP_AGENT_DISCOVERY | PROTO_CAP_STREAMING | PROTO_CAP_CONSENSUS, NULL, NULL},
    {"openai", "1.0", "OpenAI 兼容 Chat Completions API 适配器", PROTO_CAT_INTEGRATION,
     AIRY_PROTOCOL_OPENAI, PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_EMBEDDINGS,
     NULL, NULL},
    {"openjiuwen", "1.0", "OpenJiuwen 自定义二进制协议", PROTO_CAT_INTEGRATION,
     AIRY_PROTOCOL_OPENJIUWEN, PROTO_CAP_BINARY | PROTO_CAP_LOW_LATENCY | PROTO_CAP_CRC_CHECKSUM,
     NULL, NULL},
    {"openclaw", "1.0", "OpenClaw 九问平台集成适配器", PROTO_CAT_INTEGRATION,
     AIRY_PROTOCOL_OPENCLAW,
     PROTO_CAP_MULTIMODAL | PROTO_CAP_STREAMING | PROTO_CAP_AGENT_DISCOVERY |
         PROTO_CAP_TOOL_CALLING,
     NULL, NULL},
    {"Claude", "1.0", "Anthropic Claude API适配器", PROTO_CAT_INTEGRATION, AIRY_PROTOCOL_CLAUDE,
     PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_VISION |
         PROTO_CAP_EXTENDED_THINKING,
     NULL, NULL},
    {"AGNTCY", "1.0", "AGNTCY Agent Connect Protocol", PROTO_CAT_STANDARD, AIRY_PROTOCOL_AGNTCY,
     PROTO_CAP_AGENT_DISCOVERY | PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING, NULL, NULL},
    {"ChinaEco", "1.0", "国内大模型生态统一兼容适配器", PROTO_CAT_INTEGRATION,
     AIRY_PROTOCOL_CHINA_ECO,
     PROTO_CAP_STREAMING | PROTO_CAP_TOOL_CALLING | PROTO_CAP_EMBEDDINGS, NULL, NULL},
};

const proto_builtin_def_t *proto_catalog_defs(size_t *out_count)
{
    for (size_t i = 0; i < AIRY_ARRAY_SIZE(g_catalog_defs); i++) {
        const protocol_adapter_t *adapter = catalog_adapter_of(g_catalog_defs[i].type);
        g_catalog_defs[i].adapter = adapter;
        g_catalog_defs[i].context = adapter ? adapter->context : NULL;
    }

    if (out_count)
        *out_count = AIRY_ARRAY_SIZE(g_catalog_defs);
    return g_catalog_defs;
}
