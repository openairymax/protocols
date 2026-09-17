# OpenAI 企业级适配器

**位置：** `protocols/integrations/openai/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 OpenAI API 企业级特性适配器（适配器自身版本
`OPENAI_ADAPTER_VERSION` 为 `"1.0.0"`）：Chat Completions（含 Function
Calling / Tool Use）、Embeddings、流式响应、RPM/TPM 配额限流、多模型
注册与路由、指数退避重试与审计回调。适配器把 OpenAI 格式请求映射为
统一协议消息（`unified_message_t`），并通过 `openai_enterprise_get_adapter()`
提供统一 vtable 接入协议栈；`openai_enterprise_route_request()` 支持按
HTTP 路径风格入口直接分发。

## 目录结构

```
openai/
├── include/
│   └── openai_enterprise_adapter.h  # 公开头文件
└── src/
    ├── openai_enterprise_adapter.c  # 上下文、配置、vtable
    ├── openai_enterprise_adapter_model.c    # 模型注册与路由
    ├── openai_enterprise_adapter_retry.c    # 指数退避重试
    ├── openai_enterprise_adapter_utils.c    # 公共工具
    ├── openai_enterprise_adapter_embed.c    # Embeddings
    ├── openai_enterprise_adapter_ctx.c      # 上下文内部状态
    ├── openai_enterprise_adapter_chat.c     # Chat Completions / 流式
    ├── openai_enterprise_adapter_cb.c       # 回调与审计
    └── openai_enterprise_internal.h         # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `OPENAI_ADAPTER_VERSION` | `"1.0.0"` | 适配器版本 |
| `OPENAI_MAX_MODELS` | 32 | 模型注册表容量 |
| `OPENAI_MAX_FUNCTIONS` | 128 | 函数定义上限 |
| `OPENAI_MAX_MESSAGES` | 256 | 单请求消息数上限 |
| `OPENAI_MAX_TOKENS_DEFAULT` | 4096 | 默认 Token 预算 |
| `OPENAI_RATE_LIMIT_RPM` | 60 | 默认每分钟请求配额 |
| `OPENAI_RATE_LIMIT_TPM` | 100000 | 默认每分钟 Token 配额 |
| `OPENAI_MAX_RETRIES` | 3 | 默认重试次数 |
| `OPENAI_RETRY_BASE_MS` | 1000 | 重试基础间隔（毫秒） |

## 枚举

### 消息角色（`openai_role_t`，5 种）

`OPENAI_ROLE_SYSTEM`、`OPENAI_ROLE_USER`、`OPENAI_ROLE_ASSISTANT`、
`OPENAI_ROLE_TOOL`（工具结果）、`OPENAI_ROLE_FUNCTION`（旧版兼容）

### 完成原因（`openai_finish_reason_t`，5 种）

`OPENAI_FINISH_STOP`、`OPENAI_FINISH_LENGTH`、
`OPENAI_FINISH_TOOL_CALLS`、`OPENAI_FINISH_CONTENT_FILTER`、
`OPENAI_FINISH_RATE_LIMITED`

### 模型能力（`openai_model_capability_t`，位标志）

`OPENAI_MODEL_CHAT`（0x01）、`OPENAI_MODEL_EMBEDDING`（0x02）、
`OPENAI_MODEL_VISION`（0x04）、`OPENAI_MODEL_FUNCTION`（0x08）、
`OPENAI_MODEL_STREAMING`（0x10）

## 主要类型

| 类型 | 说明 |
|------|------|
| `openai_enterprise_config_t` | API key / base URL / 默认模型与采样参数、流式/函数调用/限流/审计开关、RPM/TPM 配额、Schema 严格校验 |
| `openai_enterprise_context_t` | 适配器上下文（不透明句柄） |
| `openai_model_t` | 模型条目：能力位、上下文/输出 Token 上限、每 1K Token 输入/输出成本、默认与可用性标志 |
| `openai_message_t` / `openai_tool_call_t` | 对话消息与工具调用 |
| `openai_function_def_t` / `openai_tool_def_t` | 函数与工具定义（JSON Schema） |
| `openai_chat_response_t` / `openai_embedding_response_t` / `openai_usage_t` | 响应实体与 Token 用量 |
| `openai_rate_limit_t` | 当前窗口 RPM/TPM 计量 |

## 核心 API

| 函数 | 说明 |
|------|------|
| `openai_enterprise_config_default()` | 返回默认配置 |
| `openai_enterprise_context_create(config)` / `openai_enterprise_context_destroy(ctx)` | 上下文生命周期 |
| `openai_enterprise_register_model(ctx, model)` | 注册模型 |
| `openai_enterprise_chat_completion(ctx, model, ...)` | 非流式对话补全 |
| `openai_enterprise_chat_streaming(ctx, model, ...)` | 流式对话补全 |
| `openai_enterprise_embeddings(ctx, model, ...)` | 文本向量化 |
| `openai_enterprise_list_models(ctx, &models, ...)` | 列出已注册模型 |
| `openai_enterprise_check_rate_limit(ctx, estimated_tokens)` | 请求前限流检查 |
| `openai_enterprise_set_chat_handler` / `set_embedding_handler` / `set_audit_handler` | 三类回调注册 |
| `openai_enterprise_route_request(ctx, path, ...)` | 按路径统一分发 |
| `openai_enterprise_get_adapter()` | 返回统一 `protocol_adapter_t` vtable |
| `openai_model_destroy(model)` | 释放模型条目 |

## 用法示例

```c
#include "openai_enterprise_adapter.h"

openai_enterprise_config_t cfg = openai_enterprise_config_default();
cfg.enable_streaming = true;
cfg.enable_rate_limiting = true;

openai_enterprise_context_t *ctx = openai_enterprise_context_create(&cfg);

openai_model_t model = {0};
model.id = (char *)"gpt-example";
model.capabilities = OPENAI_MODEL_CHAT | OPENAI_MODEL_STREAMING;
openai_enterprise_register_model(ctx, &model);

if (openai_enterprise_check_rate_limit(ctx, 1024)) {
    /* 在配额内，继续发起 chat_completion */
}

const protocol_adapter_t *adapter = openai_enterprise_get_adapter();
/* 注册进统一协议栈后即可参与 JSON-RPC 2.0 枢纽转换 */

openai_enterprise_context_destroy(ctx);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库；
`PROTOCOLS_ENABLE_OPENAI` 为功能开关（feature flag），默认启用。
HTTP 实发依赖可选的 libcurl（`AIRY_HAS_CURL`）。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的测试位于仓库 `tests/` 目录（`test_openai_adapter.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
