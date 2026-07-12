# Claude Adapter — Anthropic Claude API 集成适配器

> **模块路径**: `agentrt/protocols/integrations/claude/` | **版本**: v1.0.0

## 概述

`claude/` 是 AgentRT 协议栈的 Anthropic Claude API 适配器，实现 Messages API、Tool Use、Extended Thinking、Vision 等完整集成能力。将 Claude API 格式的请求转换为 AgentRT 统一协议消息，并通过 `protocol_adapter_t` 接口注册到协议路由器。

### 核心能力

| 特性 | 说明 |
|------|------|
| **Messages API** | 多轮对话、系统提示词 |
| **Tool Use** | 原生工具调用与函数执行（最多 64 个工具） |
| **Extended Thinking** | 深度推理模式（Disabled / Enabled / Extended） |
| **Vision** | 图像理解能力 |
| **Streaming** | SSE 流式响应 |
| **Prompt Caching** | 提示缓存优化（None / Ephemeral / Persistent） |
| **安全过滤** | 内容安全策略 |
| **Token 计数** | 最大上下文 200K tokens，最大输出 8192 tokens |

## 目录结构

```
claude/
├── include/
│   └── claude_adapter.h              # 适配器接口（角色/模型/工具/思考模式/缓存）
└── src/
    └── claude_adapter.c              # 适配器实现
```

## 核心数据结构

### 模型 ID

| 枚举 | 说明 |
|------|------|
| `CLAUDE_MODEL_CLAUDE_3_5_SONNET` | Claude 3.5 Sonnet |
| `CLAUDE_MODEL_CLAUDE_3_5_HAIKU` | Claude 3.5 Haiku |
| `CLAUDE_MODEL_CLAUDE_3_OPUS` | Claude 3 Opus |
| `CLAUDE_MODEL_CLAUDE_3_7_SONNET` | Claude 3.7 Sonnet |
| `CLAUDE_MODEL_CUSTOM` | 自定义模型 |

### 思考模式

| 模式 | 说明 |
|------|------|
| `CLAUDE_THINKING_DISABLED` | 禁用深度推理 |
| `CLAUDE_THINKING_ENABLED` | 启用深度推理 |
| `CLAUDE_THINKING_EXTENDED` | 扩展深度推理 |

### 缓存控制

| 类型 | 说明 |
|------|------|
| `CLAUDE_CACHE_NONE` | 不缓存 |
| `CLAUDE_CACHE_EPHEMERAL` | 临时缓存（5 分钟 TTL） |
| `CLAUDE_CACHE_PERSISTENT` | 持久缓存 |

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
| **gateway_d** | 通过协议路由器处理 Claude API 格式的入站请求 |
| **llm_d** | 通过适配器将 LLM 请求路由到 Claude 模型提供商 |

## 构建

CMake 选项: `PROTOCOLS_ENABLE_CLAUDE`（默认 ON）

```bash
cmake -S . -B build -DPROTOCOLS_ENABLE_CLAUDE=ON
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 1.0.0（Anthropic Claude API 集成适配器）