# OpenJiuwen Adapter — OpenJiuwen 平台协议适配器

> **模块路径**: `agentrt/protocols/integrations/openjiuwen/` | **版本**: v1.0.0

## 概述

`openjiuwen/` 是 AgentRT 协议栈的 OpenJiuwen 平台协议适配器，支持自定义二进制协议（Header 24B + Payload + CRC32 4B）的消息格式转换和互操作。通过 `protocol_extension_framework.h` 扩展框架注册到协议路由器。

### 核心能力

| 特性 | 说明 |
|------|------|
| **二进制协议** | Header(24B) + Payload + CRC32(4B) |
| **消息类型** | REQUEST / RESPONSE / NOTIFICATION / HEARTBEAT / ERROR |
| **连接管理** | 自动重连（指数退避，1s-60s） |
| **心跳机制** | 30s 间隔心跳 |
| **压缩/加密** | 可选载荷压缩与加密 |
| **错误处理** | 最大连续错误 5 次后触发重连 |

## 目录结构

```
openjiuwen/
├── include/
│   └── openjiuwen_adapter.h         # 适配器接口（二进制协议头/消息类型/连接状态）
└── src/
    └── openjiuwen_adapter.c         # 适配器实现
```

## 核心数据结构

### 消息类型

| 枚举 | 值 | 说明 |
|------|-----|------|
| `OPENJIUWEN_MSG_TYPE_REQUEST` | 0x0001 | 请求消息 |
| `OPENJIUWEN_MSG_TYPE_RESPONSE` | 0x0002 | 响应消息 |
| `OPENJIUWEN_MSG_TYPE_NOTIFICATION` | 0x0003 | 通知消息 |
| `OPENJIUWEN_MSG_TYPE_HEARTBEAT` | 0x0004 | 心跳消息 |
| `OPENJIUWEN_MSG_TYPE_ERROR` | 0x0005 | 错误消息 |

### 连接状态

| 状态 | 说明 |
|------|------|
| `OPENJIUWEN_CONN_DISCONNECTED` | 断开连接 |
| `OPENJIUWEN_CONN_CONNECTING` | 连接中 |
| `OPENJIUWEN_CONN_CONNECTED` | 已连接 |
| `OPENJIUWEN_CONN_RECONNECTING` | 重连中 |
| `OPENJIUWEN_CONN_ERROR` | 错误状态 |

### 二进制协议头（24 字节）

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| message_id | 0 | 4B | 消息 ID |
| timestamp | 4 | 4B | 时间戳 |
| message_type | 8 | 2B | 消息类型 |
| flags | 10 | 2B | 标志位 |
| payload_length | 12 | 4B | 载荷长度 |
| source_agent | 16 | 64B | 来源智能体 ID |
| target_agent | 80 | 64B | 目标智能体 ID |

## 上游依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| **unified_protocol.h** | `protocols/include/` | 统一消息模型 |
| **protocol_extension_framework.h** | `protocols/core/adapter/include/` | 扩展框架（二进制协议注册） |
| cJSON | 外部 | JSON 解析 |

## 下游消费者

| 消费者 | 使用方式 |
|--------|----------|
| **gateway_d** | 通过协议路由器处理 OpenJiuwen 二进制协议的入站连接 |
| **channel_d** | 通过适配器维护与 OpenJiuwen 平台的长连接 |

## 构建

默认编译，无独立 CMake 选项。

```bash
cmake -S . -B build
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 1.0.0（OpenJiuwen 二进制协议适配器）