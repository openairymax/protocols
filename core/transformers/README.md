# transformers — 协议消息转换器

**位置：** `protocols/core/transformers/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

协议消息转换器实现 JSON-RPC 2.0 与 MCP、A2A、OpenAI API、
OpenJiuwen 之间的双向消息格式转换。JSON-RPC 2.0 是内部统一中间
格式：任何两种外部协议之间的互通都经由 JSON-RPC 中转，转换时携带
一份跨调用上下文（代理、会话、追踪与 JSON-RPC ID 计数）。

## 目录结构

```
transformers/
├── include/
│   └── protocol_transformers.h     # 转换器头文件
├── src/
│   └── protocol_transformers.c     # 转换器实现
└── README.md                       # 本文件
```

## 转换上下文

`transform_context_t` 携带：`source_protocol[32]`、`target_protocol[32]`、
`agent_id[64]`、`session_id[64]`、`trace_id[64]`、`jsonrpc_id_counter`。
经 `transform_context_create(src, tgt)` 创建、`transform_context_destroy()` 释放。

## 转换器清单（13 个）

### JSON-RPC ⇄ MCP

| 函数 | 说明 |
|------|------|
| `transformer_jsonrpc_to_mcp_request()` | JSON-RPC `skill.execute` → MCP `tools/call` |
| `transformer_mcp_to_jsonrpc_response()` | MCP `tools/call` 响应 → JSON-RPC 结果 |
| `transformer_mcp_tools_list_to_jsonrpc()` | MCP `tools/list` 响应 → JSON-RPC 工具列表 |

字段映射：`params.name` → `params.name`、`params.arguments` →
`params.arguments`；回程 `result.content[]` → `result.output`。

### JSON-RPC ⇄ A2A

| 函数 | 说明 |
|------|------|
| `transformer_jsonrpc_to_a2a_task()` | JSON-RPC 任务提交 → A2A 任务 |
| `transformer_a2a_to_jsonrpc_response()` | A2A task 响应 → JSON-RPC |
| `transformer_jsonrpc_to_a2a_discover()` | JSON-RPC 代理发现 → A2A 发现 |
| `transformer_a2a_agents_to_jsonrpc()` | A2A agent card 列表 → JSON-RPC |

### JSON-RPC ⇄ OpenAI API

| 函数 | 说明 |
|------|------|
| `transformer_jsonrpc_to_openai_chat()` | JSON-RPC LLM 补全 → `/v1/chat/completions` |
| `transformer_openai_chat_to_jsonrpc()` | chat completions 响应 → JSON-RPC |
| `transformer_openai_stream_chunk_to_jsonrpc()` | streaming chunk → JSON-RPC 通知 |
| `transformer_jsonrpc_to_openai_embedding()` | JSON-RPC embedding 请求 → `/v1/embeddings` |

字段映射：`params.messages` → `messages`（role/content）、
`params.model` → `model`、`params.tools` → `tools[]`；回程
`choices[0].message.content` → `result.content`、`usage` → `result.usage`。

### JSON-RPC ⇄ OpenJiuwen

| 函数 | 说明 |
|------|------|
| `transformer_jsonrpc_to_openjiuwen()` | JSON-RPC → OpenJiuwen 二进制格式（头部 + payload + CRC32 校验尾） |
| `transformer_openjiuwen_to_jsonrpc()` | OpenJiuwen 响应 → JSON-RPC |

## 通用工具

| 函数 | 说明 |
|------|------|
| `protocol_auto_transform(source, target, target_protocol_name)` | 按端点模式自动选择转换器 |
| `protocol_validate_transformed(msg)` | 验证转换后消息完整性 |
| `protocol_list_transformers(count)` | 获取全部转换器名称列表 |

`protocol_auto_transform()` 的端点映射：

| 端点模式 | 目标协议 |
|----------|----------|
| `/mcp/(*)` | MCP |
| `/a2a/(*)` | A2A |
| `/v1/chat/(*)` | OpenAI |
| `/ojw/(*)` | OpenJiuwen |

## 用法

```c
#include "protocol_transformers.h"

transform_context_t *ctx = transform_context_create("jsonrpc", "mcp");

unified_message_t mcp_msg;
transformer_jsonrpc_to_mcp_request(&jsonrpc_msg, &mcp_msg, ctx);

unified_message_t target;
protocol_auto_transform(&source, &target, "mcp");

if (protocol_validate_transformed(&target) == 0) {
    /* 转换成功，使用 target */
}

size_t count = 0;
const char **names = protocol_list_transformers(&count);

transform_context_destroy(ctx);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
