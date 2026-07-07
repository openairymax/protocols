# AGNTCY ACP Adapter — 智能体通信协议适配器

> **模块路径**: `agentrt/protocols/standards/agntcy/` | **版本**: v0.1.0

## 概述

`agntcy/` 是 AgentRT 协议栈的 AGNTCY ACP（Agent Communication Protocol）适配器，实现面向智能体间通信的开放标准协议。AGNTCY ACP 定义了智能体注册发现、安全通道建立、结构化消息交换、跨智能体任务编排和服务等级确认五大核心能力。

### 核心能力

| 能力 | 标志 | 说明 |
|------|------|------|
| **Discovery** | `AGNTCY_CAP_DISCOVERY` | 智能体注册与发现（capability card，最多 512 个 Agent） |
| **Channel** | `AGNTCY_CAP_CHANNEL` | 智能体间安全通道建立（mutual TLS + token，最多 256 个通道） |
| **Messaging** | `AGNTCY_CAP_MESSAGING` | 结构化消息交换（同步/异步/广播/流式） |
| **Orchestrate** | `AGNTCY_CAP_ORCHESTRATE` | 跨智能体任务编排（工作流定义，最多 2048 个任务） |
| **Broadcast** | `AGNTCY_CAP_BROADCAST` | 广播消息 |
| **ACK** | `AGNTCY_CAP_ACK` | 服务等级确认与资源承诺（CPU/内存/时间） |

## 目录结构

```
agntcy/
├── include/
│   └── agntcy_acp_adapter.h         # 适配器接口（Agent Card/通道/消息/任务/ACK）
└── src/
    └── agntcy_acp_adapter.c         # 适配器实现
```

## 核心数据结构

### 消息模式

| 模式 | 说明 |
|------|------|
| `AGNTCY_MSG_SYNC` | 同步消息（请求-响应） |
| `AGNTCY_MSG_ASYNC` | 异步消息（发送即忘） |
| `AGNTCY_MSG_BROADCAST` | 广播消息（一对多） |
| `AGNTCY_MSG_STREAM` | 流式消息（持久化通道） |

### 任务状态

| 状态 | 说明 |
|------|------|
| `AGNTCY_TASK_PENDING` | 待处理 |
| `AGNTCY_TASK_DISPATCHED` | 已分发 |
| `AGNTCY_TASK_RUNNING` | 运行中 |
| `AGNTCY_TASK_COMPLETED` | 已完成 |
| `AGNTCY_TASK_FAILED` | 已失败 |
| `AGNTCY_TASK_CANCELLED` | 已取消 |

### Agent Card（能力卡片）

每个 Agent 通过 `agntcy_agent_card_t` 注册自身能力：

| 字段 | 说明 |
|------|------|
| agent_id | 唯一标识（64 字符） |
| name | 名称（128 字符） |
| capabilities_mask | 能力位掩码 |
| endpoint_url | 端点 URL（512 字符） |
| public_key_pem | 公钥 PEM（2048 字符） |
| protocol_version | 协议版本 |
| online | 在线状态 |

### 安全通道

通过 `agntcy_channel_open` 建立 mutual TLS + token 的安全通道：

| 字段 | 说明 |
|------|------|
| channel_id | 通道 ID（48 字符） |
| session_token | 会话令牌（64 字符） |
| initiator_id / responder_id | 通信双方 Agent ID |
| encrypted | 是否加密 |

### ACK（服务等级确认）

通过 `agntcy_ack_negotiate` 进行资源承诺协商：

| 字段 | 说明 |
|------|------|
| resource_type | 资源类型 |
| cpu_cores | CPU 核心数 |
| memory_kb | 内存大小（KB） |
| requested_amount / guaranteed_amount | 请求量 / 保证量 |
| valid_until | 有效期 |

## 上游依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| **unified_protocol.h** | `protocols/include/` | 统一消息模型 |
| cJSON | 外部 | JSON 解析 |
| libcurl | 外部 | HTTP 客户端 |

## 下游消费者

| 消费者 | 使用方式 |
|--------|----------|
| **gateway_d** | 通过协议路由器处理 AGNTCY ACP 协议的 Agent 间通信 |
| **channel_d** | 通过适配器管理 Agent 间的安全通信通道 |
| **sched_d** | 通过 AGNTCY 任务编排进行跨 Agent 工作流调度 |

## 构建

默认编译，无独立 CMake 选项。

```bash
cmake -S . -B build
cmake --build build --target agentrt_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 0.1.0（AGNTCY ACP 智能体通信协议适配器）