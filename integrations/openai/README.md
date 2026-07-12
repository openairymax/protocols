# OpenAI Enterprise Adapter — OpenAI API 企业级适配器

> **模块路径**: `agentrt/protocols/integrations/openai/` | **版本**: v1.0.0

## 概述

`openai/` 是 AgentRT 协议栈的 OpenAI API 企业级特性适配器，实现完整的 Chat Completions、Embeddings、Function Calling、Streaming、Rate Limiting 等企业级能力。将 OpenAI API 格式的请求转换为 AgentRT 统一协议消息（`unified_message_t`），并通过 `protocol_adapter_t` 接口注册到协议路由器。

### 核心能力

| 特性 | 说明 |
|------|------|
| **Chat Completions** | 多轮对话，含 Function Calling / Tool Use |
| **Embeddings API** | 文本向量化 |
| **Streaming SSE** | 流式响应 |
| **Rate Limiting** | RPM/TPM 配额管理（默认 60 RPM / 100K TPM） |
| **多模型路由** | 模型注册与回退策略（最多 32 个模型） |
| **Token 预算** | 控制最大 Token 消耗（默认 4096） |
| **请求重试** | 指数退避重试（默认 3 次，基础间隔 1000ms） |
| **审计日志** | 请求/响应/Token 使用记录 |

## 目录结构

```
openai/
├── include/
│   └── openai_enterprise_adapter.h   # 适配器接口（角色/模型/消息/工具调用/响应）
└── src/
    └── openai_enterprise_adapter.c   # 适配器实现
```

## 核心数据结构

### 角色类型

| 枚举 | 说明 |
|------|------|
| `OPENAI_ROLE_SYSTEM` | 系统提示词 |
| `OPENAI_ROLE_USER` | 用户消息 |
| `OPENAI_ROLE_ASSISTANT` | 助手回复 |
| `OPENAI_ROLE_TOOL` | 工具调用结果 |
| `OPENAI_ROLE_FUNCTION` | 函数调用（兼容旧版） |

### 模型能力

| 标志 | 说明 |
|------|------|
| `OPENAI_MODEL_CHAT` | 对话模型 |
| `OPENAI_MODEL_EMBEDDING` | 嵌入模型 |
| `OPENAI_MODEL_VISION` | 视觉模型 |
| `OPENAI_MODEL_FUNCTION` | 函数调用 |
| `OPENAI_MODEL_STREAMING` | 流式响应 |

### 完成原因

| 枚举 | 说明 |
|------|------|
| `OPENAI_FINISH_STOP` | 正常完成 |
| `OPENAI_FINISH_LENGTH` | 达到最大 Token 限制 |
| `OPENAI_FINISH_TOOL_CALLS` | 触发工具调用 |
| `OPENAI_FINISH_CONTENT_FILTER` | 内容过滤 |
| `OPENAI_FINISH_RATE_LIMITED` | 速率限制 |

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
| **gateway_d** | 通过 `gateway_openai_compat` 处理 OpenAI API 格式的入站请求 |
| **llm_d** | 通过适配器将 LLM 请求路由到 OpenAI 兼容的模型提供商 |

## 构建

CMake 选项: `PROTOCOLS_ENABLE_OPENAI`（默认编译）

```bash
cmake -S . -B build -DPROTOCOLS_ENABLE_OPENAI=ON
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 1.0.0（OpenAI API 企业级适配器）