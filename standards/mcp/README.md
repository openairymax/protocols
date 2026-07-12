# MCP v1.0 Adapter — Model Context Protocol 协议适配器

> **模块路径**: `agentrt/protocols/standards/mcp/` | **版本**: v1.0.0

## 概述

`mcp/` 是 AgentRT 协议栈的 MCP v1.0（Model Context Protocol）协议适配器，实现 MCP 规范定义的工具发现、调用、资源访问、采样等核心能力。MCP 是 Anthropic 提出的开放标准协议，用于 AI 模型与外部工具/数据源之间的上下文交互，在 AgentRT 中承担 Agent-to-Tool（A2T）协议的角色。

### 核心能力

| 能力 | 标志 | 说明 |
|------|------|------|
| **Tools** | `MCP_CAP_TOOLS` | tools/list + tools/call — 工具发现与调用（最多 1024 个工具） |
| **Resources** | `MCP_CAP_RESOURCES` | resources/list + resources/read + resources/templates — 资源管理（最多 512 个资源） |
| **Prompts** | `MCP_CAP_PROMPTS` | prompts/list + prompts/get — 提示模板管理（最多 256 个模板） |
| **Sampling** | `MCP_CAP_SAMPLING` | sampling/createMessage — LLM 采样请求 |
| **Logging** | `MCP_CAP_LOGGING` | logging/setLogLevel — 日志级别控制 |
| **Completion** | `MCP_CAP_COMPLETION` | completion/complete — 自动补全 |

## 目录结构

```
mcp/
├── include/
│   ├── mcp_v1_adapter.h             # 适配器接口（工具/资源/提示/采样/日志/完成）
│   └── mcp_transport.h              # 传输层抽象（HTTP/WS/stdio）
└── src/
    ├── mcp_v1_adapter.c             # 适配器实现
    └── mcp_transport.c              # 传输层实现
```

## 核心数据结构

### 内容类型

| 类型 | 说明 |
|------|------|
| `MCP_CONTENT_TEXT` | 文本内容 |
| `MCP_CONTENT_IMAGE` | 图像内容 |
| `MCP_CONTENT_RESOURCE` | 资源引用 |
| `MCP_CONTENT_EMBEDDED` | 嵌入式资源 |

### 日志级别（8 级，对齐 syslog）

| 级别 | 说明 |
|------|------|
| `MCP_LOG_DEBUG` | 调试 |
| `MCP_LOG_INFO` | 信息 |
| `MCP_LOG_NOTICE` | 注意 |
| `MCP_LOG_WARNING` | 警告 |
| `MCP_LOG_ERROR` | 错误 |
| `MCP_LOG_CRITICAL` | 严重 |
| `MCP_LOG_ALERT` | 告警 |
| `MCP_LOG_EMERGENCY` | 紧急 |

### 传输层（mcp_transport）

MCP 适配器支持三种传输方式：

| 传输 | 说明 |
|------|------|
| HTTP | 标准 HTTP REST API |
| WebSocket | 双向实时通信 |
| Stdio | 标准输入/输出（本地进程通信） |

## 上游依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| **unified_protocol.h** | `protocols/include/` | 统一消息模型 |
| **airy_protocol_interface.h** | `protocols/include/` | 适配器虚表与接口定义 |
| cJSON | 外部 | JSON 解析 |
| libcurl | 外部 | HTTP 客户端 |

## 下游消费者

| 消费者 | 使用方式 |
|--------|----------|
| **gateway_d** | 通过 `gateway_mcp_server` 处理 MCP 客户端请求，暴露 AgentRT 工具和资源 |
| **tool_d** | 通过 MCP 协议将工具注册暴露给外部 MCP 客户端 |
| **llm_d** | 通过 MCP sampling 能力请求 LLM 采样 |

## 构建

默认编译，无独立 CMake 选项。

```bash
cmake -S . -B build
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 1.0.0（MCP v1.0 协议适配器）