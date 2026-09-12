# Claude 适配器

**位置：** `protocols/integrations/claude/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 Anthropic Claude Messages API 适配器（适配器版本
`CLAUDE_ADAPTER_VERSION` 为 `"1.0.0"`，模拟的 API 版本
`CLAUDE_API_VERSION` 为 `"2023-06-01"`）：多轮对话与系统提示词、原生
Tool Use、Extended Thinking 三档推理模式、Vision 多模态内容块、SSE
流式响应、Prompt Caching 三档缓存控制与 Token 计数。请求经映射后进入
统一协议消息模型，并通过 `claude_get_protocol_adapter()` 提供统一
vtable 接入协议栈。

## 目录结构

```
claude/
├── include/
│   └── claude_adapter.h        # 公开头文件
└── src/
    ├── claude_adapter.c        # 上下文、配置、vtable
    ├── claude_adapter_proto.c  # 统一协议消息互转
    ├── claude_adapter_http.c   # HTTP 请求与重试
    ├── claude_adapter_model.c  # 模型清单与能力
    ├── claude_adapter_cleanup.c # 实体释放
    └── claude_adapter_internal.h # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `CLAUDE_ADAPTER_VERSION` | `"1.0.0"` | 适配器版本 |
| `CLAUDE_API_VERSION` | `"2023-06-01"` | 对应 Messages API 版本 |
| `CLAUDE_MAX_MODELS` | 16 | 模型注册容量 |
| `CLAUDE_MAX_TOOLS` | 64 | 工具定义上限 |
| `CLAUDE_MAX_MESSAGES` | 128 | 单请求消息数上限 |
| `CLAUDE_MAX_CONTEXT_TOKENS` | 200000 | 最大上下文 Token |
| `CLAUDE_MAX_OUTPUT_TOKENS` | 8192 | 最大输出 Token |
| `CLAUDE_DEFAULT_TIMEOUT_MS` | 60000 | 默认超时（毫秒） |
| `CLAUDE_MAX_RETRIES` | 5 | 默认重试次数 |

## 枚举

### 模型（`claude_model_id_t`，7 项）

`CLAUDE_MODEL_CLAUDE_3_5_SONNET`、`CLAUDE_MODEL_CLAUDE_3_5_HAIKU`、
`CLAUDE_MODEL_CLAUDE_3_OPUS`、`CLAUDE_MODEL_CLAUDE_3_SONNET`、
`CLAUDE_MODEL_CLAUDE_3_HAIKU`、`CLAUDE_MODEL_CLAUDE_3_7_SONNET`、
`CLAUDE_MODEL_CUSTOM`

### 角色（`claude_role_t`，3 种）

`CLAUDE_ROLE_USER`、`CLAUDE_ROLE_ASSISTANT`、`CLAUDE_ROLE_SYSTEM`

### 停止原因（`claude_stop_reason_t`，4 种）

`CLAUDE_STOP_END_TURN`、`CLAUDE_STOP_MAX_TOKENS`、
`CLAUDE_STOP_TOOL_USE`、`CLAUDE_STOP_SEQUENCE`

### 思考模式（`claude_thinking_mode_t`，3 档）

`CLAUDE_THINKING_DISABLED`、`CLAUDE_THINKING_ENABLED`、
`CLAUDE_THINKING_EXTENDED`

### 缓存控制（`claude_cache_control_t`，3 档）

`CLAUDE_CACHE_NONE`、`CLAUDE_CACHE_EPHEMERAL`、`CLAUDE_CACHE_PERSISTENT`

## 主要类型

| 类型 | 说明 |
|------|------|
| `claude_config_t` | 上下文配置（API key、模型、思考/缓存模式等） |
| `claude_adapter_context_t` | 适配器上下文（不透明句柄） |
| `claude_message_t` / `claude_content_block_t` | 对话消息与多模态内容块 |
| `claude_tool_def_t` / `claude_tool_use_t` / `claude_tool_result_t` | 工具定义、调用与结果 |
| `claude_response_t` / `claude_stream_event_t` | 响应实体与流式事件 |
| `claude_model_info_t` | 模型信息条目 |

## 核心 API

| 函数 | 说明 |
|------|------|
| `claude_config_default()` | 返回默认配置 |
| `claude_adapter_create(config)` / `claude_adapter_destroy(ctx)` | 上下文生命周期 |
| `claude_adapter_is_initialized(ctx)` / `claude_adapter_version()` | 状态与版本查询 |
| `claude_messages_create(ctx, messages, ...)` | 非流式 Messages API 调用 |
| `claude_messages_stream(ctx, messages, ...)` | SSE 流式调用 |
| `claude_count_tokens(ctx, messages, ...)` | Token 计数 |
| `claude_list_models(ctx, &models, &count)` | 列出模型 |
| `claude_set_message_handler` / `claude_set_stream_handler` / `claude_set_tool_use_handler` | 三类回调注册 |
| `claude_get_usage_statistics(ctx, stats_json, ...)` | 用量统计（JSON 输出） |
| `claude_get_protocol_adapter()` | 返回统一 vtable（`proto_adapter_t`） |
| `claude_response_destroy` / `claude_message_destroy` / `claude_tool_def_destroy` / `claude_model_info_destroy` / `claude_stream_event_destroy` | 实体释放 |

## 用法示例

```c
#include "claude_adapter.h"

claude_config_t cfg = claude_config_default();
claude_adapter_context_t *ctx = claude_adapter_create(&cfg);

claude_message_t msg = {0};
msg.role = CLAUDE_ROLE_USER;
msg.content = (char *)"Summarize this design doc";

claude_response_t resp = {0};
claude_messages_create(ctx, &msg, 1, NULL, 0, NULL, &resp);
/* 使用 resp ... */
claude_response_destroy(&resp);

const proto_adapter_t *adapter = claude_get_protocol_adapter();
/* 注册进统一协议栈 */

claude_adapter_destroy(ctx);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_CLAUDE` 门控（默认 `ON`）；
关闭后不参与编译。HTTP 实发依赖可选的 libcurl（`AIRY_HAS_CURL`）。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的测试位于仓库 `tests/` 目录（`test_claude_adapter.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
