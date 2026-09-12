# AGNTCY ACP 0.1.0 协议适配器

**位置：** `protocols/standards/agntcy/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 AGNTCY ACP（Agent Communication Protocol）0.1.0 适配器，
提供智能体注册发现、通道建立、结构化消息交换（同步/异步/广播/流式）、
跨智能体任务编排与资源承诺 ACK 协商五组能力。适配器为纯内存会话模型：
`agntcy_handle_t` 句柄内维护 Agent 表、通道表与任务表；通道以会话令牌
与有效期标识并置加密标志，传输层加密由承载方（如网关的 TLS 终结）负责。

## 目录结构

```
agntcy/
├── include/
│   └── agntcy_acp_adapter.h    # 公开头文件：常量、类型、全部 API
└── src/
    └── agntcy_acp_adapter.c    # 单文件实现（注册/通道/消息/任务/ACK）
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `AGNTCY_ACP_VERSION` | `"0.1.0"` | 协议版本字符串 |
| `AGNTCY_ACP_PROTOCOL_NAME` | `"agntcy"` | 协议名 |
| `AGNTCY_ACP_MAX_AGENTS` | 512 | Agent 表容量 |
| `AGNTCY_ACP_MAX_CAPABILITIES` | 128 | 能力条目上限 |
| `AGNTCY_ACP_MAX_TASKS` | 2048 | 任务表容量 |
| `AGNTCY_ACP_MAX_CHANNELS` | 256 | 通道表容量 |
| `AGNTCY_ACP_MAX_MESSAGE_SIZE` | 8 MB | 单条消息上限 |
| `AGNTCY_ACP_DEFAULT_TIMEOUT_MS` | 30000 | 默认超时（毫秒） |
| `AGNTCY_ACP_TOKEN_SIZE` | 64 | 会话令牌缓冲区长度 |
| `AGNTCY_ACP_CHANNEL_ID_SIZE` | 48 | 通道 ID 缓冲区长度 |

## 枚举

### Agent 能力（`agntcy_capability_t`，位标志）

| 标志 | 值 | 说明 |
|------|----|------|
| `AGNTCY_CAP_DISCOVERY` | 0x01 | 注册与发现 |
| `AGNTCY_CAP_CHANNEL` | 0x02 | 通道建立 |
| `AGNTCY_CAP_MESSAGING` | 0x04 | 消息交换 |
| `AGNTCY_CAP_ORCHESTRATE` | 0x08 | 任务编排 |
| `AGNTCY_CAP_BROADCAST` | 0x10 | 广播 |
| `AGNTCY_CAP_ACK` | 0x20 | 资源承诺确认 |

### 消息模式（`agntcy_message_mode_t`，4 种）

`AGNTCY_MSG_SYNC`（请求-响应）、`AGNTCY_MSG_ASYNC`（发送即忘）、
`AGNTCY_MSG_BROADCAST`（一对多）、`AGNTCY_MSG_STREAM`（持久通道）

### 任务状态（`agntcy_task_state_t`，6 态）

`AGNTCY_TASK_PENDING` → `AGNTCY_TASK_DISPATCHED` → `AGNTCY_TASK_RUNNING`
→ 终态 `AGNTCY_TASK_COMPLETED` / `AGNTCY_TASK_FAILED` /
`AGNTCY_TASK_CANCELLED`

## 主要类型

| 类型 | 关键字段 | 说明 |
|------|----------|------|
| `agntcy_agent_card_t` | `agent_id[64]`、`name[128]`、`capabilities_mask`、`endpoint_url[512]`、`public_key_pem[2048]`、`online` | Agent 能力卡片 |
| `agntcy_channel_t` | `channel_id`、`initiator_id` / `responder_id`、`session_token`、`expires_at`、`encrypted` | 通信通道 |
| `agntcy_message_t` | `message_id`、`mode`、收发双方 ID、`payload`、`content_type`、`priority` | 消息实体 |
| `agntcy_task_t` | `task_id`、`workflow_json`、`assigned_agent_ids`、`state`、`deadline_at` | 编排任务 |
| `agntcy_ack_t` | `resource_type`、`requested_amount` / `guaranteed_amount`、`cpu_cores`、`memory_kb`、`committed` | 资源承诺 |
| `agntcy_handle_t` | Agent/通道/任务三张表 + 计数器 | 会话句柄（由 create/destroy 管理） |

## 核心 API

| 函数 | 说明 |
|------|------|
| `agntcy_acp_create(&handle)` / `agntcy_acp_destroy(handle)` | 句柄生命周期 |
| `agntcy_agent_register(h, card)` / `agntcy_agent_unregister(h, agent_id)` | Agent 注册与注销 |
| `agntcy_agent_discover(h, cap_mask, results, ...)` | 按能力位掩码发现 Agent |
| `agntcy_channel_open(h, initiator_id, responder_id, &channel)` / `agntcy_channel_close(h, channel_id)` | 通道建立（回填通道实体）与关闭 |
| `agntcy_message_send(h, msg, response, &resp_size)` | 按消息模式发送（SYNC 模式回填响应） |
| `agntcy_message_broadcast(h, msg)` | 广播消息 |
| `agntcy_task_orchestrate(h, task_id, workflow_json)` | 以 JSON 工作流定义发起编排任务 |
| `agntcy_task_get_state(h, task_id, &state)` | 查询任务状态 |
| `agntcy_ack_negotiate(h, agent_id, ack_request, ...)` | 资源承诺协商 |

## 用法示例

```c
#include "agntcy_acp_adapter.h"

agntcy_handle_t *h = NULL;
agntcy_acp_create(&h);

agntcy_agent_card_t card = {0};
snprintf(card.agent_id, sizeof(card.agent_id), "agent-a");
card.capabilities_mask = AGNTCY_CAP_MESSAGING | AGNTCY_CAP_ACK;
card.online = true;
agntcy_agent_register(h, &card);

agntcy_channel_t ch = {0};
agntcy_channel_open(h, "agent-a", "agent-b", &ch);
/* ch.channel_id / ch.session_token 已由适配器填充 */

agntcy_task_state_t state;
agntcy_task_get_state(h, "task-1", &state);

agntcy_acp_destroy(h);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_AGNTCY` 门控（默认 `ON`）；
关闭该选项后源码不参与编译。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的测试位于仓库 `tests/` 目录（`test_agntcy_acp.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
