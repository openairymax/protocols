# A2A v0.3 协议适配器

**位置：** `protocols/standards/a2a/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 Agent-to-Agent Protocol（A2A）v0.3 的深度适配器，覆盖该协议
规范的六大核心能力：Agent Card 发现、任务生命周期管理、结构化消息交换、
任务协商、流式进度推送与事件驱动的推送通知，并附带一套认证与会话管理
设施（API Key / HMAC-SHA256 请求签名、AES-GCM 载荷加密参数体系）。
适配器同时以 JSON-RPC 2.0 风格方法名提供 `a2a_v03_route_request()`
统一入口，便于网关侧按方法字符串分发。

## 目录结构

```
a2a/
├── include/
│   └── a2a_v03_adapter.h        # 公开头文件：常量、类型、全部 API
└── src/
    ├── a2a_v03_adapter.c        # 上下文生命周期、配置、适配器 vtable
    ├── a2a_v03_adapter_agent.c  # Agent Card 注册/注销/发现
    ├── a2a_v03_adapter_task.c   # 任务创建/更新/取消/查询
    ├── a2a_v03_adapter_msg.c    # 消息交换与协商
    ├── a2a_v03_adapter_cb.c     # 回调、流式、传输适配、路由分发
    ├── a2a_v03_adapter_auth.c   # 认证、令牌、会话、签名
    └── a2a_v03_adapter_internal.h  # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `A2A_V03_VERSION` | `"0.3.0"` | 协议版本字符串 |
| `A2A_V03_PROTOCOL_NAME` | `"a2a"` | 协议名 |
| `A2A_V03_MAX_AGENTS` | 256 | 默认 Agent 注册表容量 |
| `A2A_V03_MAX_TASKS` | 4096 | 默认任务表容量 |
| `A2A_V03_MAX_CAPABILITIES` | 64 | 单 Agent 能力上限 |
| `A2A_V03_MAX_MESSAGE_SIZE` | 16 MB | 单条消息上限 |
| `A2A_V03_DEFAULT_TIMEOUT_MS` | 60000 | 默认超时（毫秒） |
| `A2A_AUTH_TOKEN_SIZE` | 64 | 认证令牌缓冲区长度 |
| `A2A_AUTH_SECRET_MAX_LEN` | 128 | 共享密钥最大长度 |
| `A2A_CRYPTO_NONCE_SIZE` / `A2A_CRYPTO_TAG_SIZE` | 16 / 16 | GCM nonce / 认证标签 |
| `A2A_CRYPTO_KEY_SIZE` | 32 | GCM 密钥长度（AES-256） |
| `A2A_SESSION_ID_SIZE` / `A2A_TASK_ID_SIZE` | 36 / 40 | 会话 / 任务 ID 缓冲区 |
| `A2A_MAX_FAILED_AUTH_ATTEMPTS` | 5 | 连续认证失败上限 |
| `A2A_TOKEN_EXPIRY_SEC` | 3600 | 令牌有效期（秒） |

## 枚举

### Agent 能力（`a2a_capability_t`，位标志）

`A2A_CAP_TASK_EXECUTION`（0x01）、`A2A_CAP_STREAMING`（0x02）、
`A2A_CAP_PUSH_NOTIFICATIONS`（0x04）、`A2A_CAP_NEGOTIATION`（0x08）、
`A2A_CAP_MULTI_TURN`（0x10）、`A2A_CAP_STATE_TRANSITION`（0x20）

### 任务状态（`a2a_task_state_t`，7 态）

`A2A_TASK_SUBMITTED` → `A2A_TASK_WORKING` →（可回到
`A2A_TASK_INPUT_REQUIRED`）→ 终态 `A2A_TASK_COMPLETED` /
`A2A_TASK_CANCELED` / `A2A_TASK_FAILED` / `A2A_TASK_REJECTED`

### 消息类型（`a2a_message_type_t`，4 型）

`A2A_MSG_TEXT`、`A2A_MSG_FILE`、`A2A_MSG_STRUCTURED`、`A2A_MSG_ERROR`

### 协商动作（`a2a_negotiation_action_t`，4 种）

`A2A_NEGOTIATE_PROPOSE`、`A2A_NEGOTIATE_ACCEPT`、
`A2A_NEGOTIATE_REJECT`、`A2A_NEGOTIATE_COUNTER`

### 认证与加密方法

- `a2a_auth_method_t`：`A2A_AUTH_NONE`、`A2A_AUTH_API_KEY`、
  `A2A_AUTH_HMAC_SHA256`（请求签名）、`A2A_AUTH_JWT_BEARER`（预留）
- `a2a_crypto_method_t`：`A2A_CRYPTO_NONE`、`A2A_CRYPTO_AES_128_GCM`、
  `A2A_CRYPTO_AES_256_GCM`
- 辅助字符串化：`a2a_auth_method_string()`、`a2a_crypto_method_string()`

## 主要类型

| 类型 | 说明 |
|------|------|
| `a2a_v03_config_t` | 上下文配置：能力位、超时、Agent/任务容量、消息上限、协商/流式/推送开关、是否强制认证 |
| `a2a_v03_context_t` | 适配器上下文（不透明句柄） |
| `a2a_agent_card_t` / `a2a_skill_t` | Agent Card 及其技能条目（名称/描述/JSON Schema） |
| `a2a_agent_list_t` / `a2a_discovery_filter_t` | 发现结果列表与过滤条件 |
| `a2a_task_t` / `a2a_message_t` | 任务与消息实体 |
| `a2a_negotiation_t` / `a2a_notification_t` | 协商提案与推送通知 |
| `a2a_auth_config_t` / `a2a_auth_token_t` / `a2a_session_t` | 认证配置、令牌与会话 |

## 核心 API

### 生命周期与查询

| 函数 | 说明 |
|------|------|
| `a2a_v03_config_default()` | 返回默认配置 |
| `a2a_v03_context_create(config)` / `a2a_v03_context_destroy(ctx)` | 创建/销毁上下文 |
| `a2a_v03_get_adapter()` | 返回统一 `protocol_adapter_t` vtable |
| `a2a_v03_get_agent_count(ctx)` / `a2a_v03_get_task_count(ctx)` | 注册 Agent 数 / 在管任务数 |

### Agent 与任务

| 函数 | 说明 |
|------|------|
| `a2a_v03_register_agent` / `a2a_v03_unregister_agent` / `a2a_v03_get_agent_card` | Agent Card 注册、注销、查询 |
| `a2a_v03_discover_agents(ctx, capability, skill_name, ...)` | 按能力/技能发现 Agent |
| `a2a_v03_create_task` / `a2a_v03_update_task` / `a2a_v03_cancel_task` / `a2a_v03_get_task` | 任务生命周期 |

### 消息、协商、通知与流式

| 函数 | 说明 |
|------|------|
| `a2a_v03_send_message` | 向目标 Agent 发送消息 |
| `a2a_v03_negotiate(ctx, proposal, ...)` | 提交协商提案并获取响应 |
| `a2a_v03_subscribe_notifications` / `a2a_v03_unsubscribe_notifications` / `a2a_v03_send_notification` | 推送通知订阅与发送 |
| `a2a_v03_stream_task_update(ctx, task_id, progress, ...)` | 流式任务进度更新 |

### 回调、传输与路由

| 函数 | 说明 |
|------|------|
| `a2a_v03_set_task_handler` / `set_message_handler` / `set_negotiation_handler` / `set_streaming_handler` | 四类业务回调注册 |
| `a2a_v03_set_transport(ctx, write_fn, user_data)` | 注入字节流写回调 |
| `a2a_v03_set_transport_read(...)` | 注入字节流读回调 |
| `a2a_v03_route_request(ctx, method, params_json, ...)` | 按 JSON-RPC 方法名统一分发 |

### 认证与会话

| 函数 | 说明 |
|------|------|
| `a2a_v03_auth_init` / `a2a_v03_auth_shutdown` | 认证子系统初始化/关闭 |
| `a2a_v03_authenticate` / `a2a_v03_verify_token` / `a2a_v03_invalidate_token` | 认证、令牌校验与吊销 |
| `a2a_v03_sign_request` / `a2a_v03_verify_signature` | HMAC-SHA256 请求签名与验签 |
| `a2a_v03_create_session` / `a2a_v03_validate_session` / `a2a_v03_destroy_session` / `a2a_v03_get_active_session_count` | 会话管理 |

### 实体释放

`a2a_agent_card_destroy`、`a2a_task_destroy`、`a2a_message_destroy`、
`a2a_negotiation_destroy`

## 用法示例

```c
#include "a2a_v03_adapter.h"

a2a_v03_config_t cfg = a2a_v03_config_default();
a2a_v03_context_t *ctx = a2a_v03_context_create(&cfg);

a2a_agent_card_t card = {0};
/* ... 填充 card.name / card.capabilities / card.skills ... */
a2a_v03_register_agent(ctx, &card);

a2a_task_t *task = NULL;
a2a_v03_create_task(ctx, "agent-b", "summarize report",
                    "{\"input\":\"weekly data\"}", &task);
a2a_v03_update_task(ctx, task->id, A2A_TASK_WORKING, NULL, 0.3);

const protocol_adapter_t *adapter = a2a_v03_get_adapter();
/* 亦可将 adapter 注册进统一协议栈（见主文档） */

a2a_task_destroy(task);
a2a_v03_context_destroy(ctx);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
`PROTOCOLS_ENABLE_A2A` 为功能开关（feature flag），默认启用。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的单元/集成测试位于仓库 `tests/` 目录（`test_a2a_adapter.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
