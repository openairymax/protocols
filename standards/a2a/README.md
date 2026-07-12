# A2A v0.3 Adapter — Agent-to-Agent 协议适配器

> **模块路径**: `agentrt/protocols/standards/a2a/` | **版本**: v0.3.0

## 概述

`a2a/` 是 AgentRT 协议栈的 A2A v0.3.0（Agent-to-Agent）协议适配器，实现智能体发现、任务委派、协商与协作的完整 A2A 协议能力。A2A 是 Google 提出的开放标准协议，用于 AI Agent 之间的互操作通信。

### 核心能力

| 能力 | 标志 | 说明 |
|------|------|------|
| **Agent Card** | `A2A_CAP_TASK_EXECUTION` | 智能体能力描述与发现（最多 256 个 Agent） |
| **Task Lifecycle** | — | 任务创建/更新/取消/完成（7 种状态） |
| **Message Exchange** | `A2A_CAP_MULTI_TURN` | 智能体间结构化消息传递（4 种类型） |
| **Negotiation** | `A2A_CAP_NEGOTIATION` | 任务协商与条件匹配 |
| **Streaming** | `A2A_CAP_STREAMING` | 流式任务执行与进度推送 |
| **Push Notifications** | `A2A_CAP_PUSH_NOTIFICATIONS` | 事件驱动的通知机制 |
| **State Transition** | `A2A_CAP_STATE_TRANSITION` | 状态转换 |

## 目录结构

```
a2a/
├── include/
│   └── a2a_v03_adapter.h            # 适配器接口（Agent Card/任务/消息/协商/认证加密）
└── src/
    └── a2a_v03_adapter.c            # 适配器实现
```

## 核心数据结构

### 任务状态（7 种）

| 状态 | 说明 |
|------|------|
| `A2A_TASK_SUBMITTED` | 已提交 |
| `A2A_TASK_WORKING` | 处理中 |
| `A2A_TASK_INPUT_REQUIRED` | 需要输入 |
| `A2A_TASK_COMPLETED` | 已完成 |
| `A2A_TASK_CANCELED` | 已取消 |
| `A2A_TASK_FAILED` | 已失败 |
| `A2A_TASK_REJECTED` | 已拒绝 |

### 消息类型

| 类型 | 说明 |
|------|------|
| `A2A_MSG_TEXT` | 文本消息 |
| `A2A_MSG_FILE` | 文件消息 |
| `A2A_MSG_STRUCTURED` | 结构化消息 |
| `A2A_MSG_ERROR` | 错误消息 |

### 认证与加密（PROTO-002）

| 常量 | 值 | 说明 |
|------|-----|------|
| A2A 认证 Token | 64 字节 | 会话认证令牌 |
| 加密 Nonce | 16 字节 | 加密随机数 |
| 加密 Tag | 16 字节 | 认证标签 |
| 加密密钥 | 32 字节 | AES-256-GCM 密钥 |
| 会话 ID | 36 字节 | UUID 格式 |
| Token 过期 | 3600 秒 | 1 小时 |

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
| **gateway_d** | 通过 `gateway_a2a_handler` 处理 A2A 协议的 Agent 间通信 |
| **channel_d** | 通过适配器管理 Agent 间的通信通道 |
| **sched_d** | 通过 A2A 任务委派机制进行跨 Agent 任务调度 |

## 构建

默认编译，无独立 CMake 选项。

```bash
cmake -S . -B build
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 0.1.0（A2A v0.3.0 协议适配器）