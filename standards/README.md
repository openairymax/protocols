# Standards — 标准协议适配层

> **模块路径**: `agentrt/protocols/standards/` | **版本**: v0.1.0

## 概述

`standards/` 是 AgentRT 协议栈的标准协议适配层，包含三个开放标准协议适配器，负责将外部标准协议映射到 AgentRT 的统一协议体系。与 `integrations/`（平台集成适配）不同，`standards/` 实现的是行业通用开放标准。

### 支持的协议

| 协议 | 目录 | 版本 | 类型 | 说明 |
|------|------|------|------|------|
| **A2A** | `a2a/` | v0.3.0 | Agent-to-Agent | Google 提出的智能体间通信标准 |
| **MCP** | `mcp/` | v1.0.0 | Agent-to-Tool | Anthropic 提出的模型上下文协议 |
| **AGNTCY ACP** | `agntcy/` | v0.1.0 | Agent Communication | 智能体通信协议（注册/发现/通道/消息/编排） |

## 目录结构

```
standards/
├── README.md                         # 本文件
├── a2a/                              # A2A v0.3.0 协议适配器
│   ├── include/a2a_v03_adapter.h
│   └── src/a2a_v03_adapter.c
├── mcp/                              # MCP v1.0 协议适配器
│   ├── include/mcp_v1_adapter.h
│   ├── include/mcp_transport.h
│   ├── src/mcp_v1_adapter.c
│   └── src/mcp_transport.c
└── agntcy/                           # AGNTCY ACP 协议适配器
    ├── include/agntcy_acp_adapter.h
    └── src/agntcy_acp_adapter.c
```

## 协议对比

| 维度 | A2A | MCP | AGNTCY ACP |
|------|-----|-----|------------|
| **通信对象** | Agent ↔ Agent | Agent ↔ Tool/Data | Agent ↔ Agent |
| **核心原语** | Agent Card / Task / Message | Tool / Resource / Prompt / Sampling | Agent Card / Channel / Message / Task / ACK |
| **安全模型** | Token + AES-256-GCM | 传输层 TLS | Mutual TLS + Token |
| **最大 Agent** | 256 | N/A | 512 |
| **最大工具** | N/A | 1024 | N/A |
| **最大任务** | 4096 | N/A | 2048 |
| **消息大小** | 16 MB | 10 MB | 8 MB |
| **默认超时** | 60s | 30s | 30s |

## 架构定位

```
协议栈分层:
  integrations/  ← 平台集成适配（OpenAI / Claude / OpenJiuwen / OpenClaw / 国内生态）
  ★ standards/  ★  ← 开放标准协议适配（A2A / MCP / AGNTCY ACP）
  core/           ← 核心路由 / 扩展框架 / 转换 / 注册表
  common/         ← 统一协议接口 / 消息模型
  include/        ← 顶层公共接口（unified_protocol.h / agentrt_protocol_interface.h）
```

## 上游依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| **unified_protocol.h** | `protocols/include/` | 统一消息模型 |
| **agentrt_protocol_interface.h** | `protocols/include/` | 适配器虚表与接口定义 |
| cJSON | 外部 | JSON 解析 |
| libcurl | 外部 | HTTP 客户端 |

## 下游消费者

| 消费者 | 使用方式 |
|--------|----------|
| **gateway_d** | 通过 `gateway_a2a_handler` / `gateway_mcp_server` 处理标准协议请求 |
| **channel_d** | 通过 A2A / AGNTCY 适配器管理 Agent 间通信通道 |
| **tool_d** | 通过 MCP 协议暴露工具注册接口 |
| **sched_d** | 通过 A2A / AGNTCY 任务委派机制进行跨 Agent 任务调度 |

## 构建

全部三个协议适配器默认编译，编译为 `libagentrt_protocols` 的一部分。

```bash
cmake -S . -B build
cmake --build build --target agentrt_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 0.1.0（标准协议适配层：A2A + MCP + AGNTCY ACP）