# OpenClaw Adapter — OpenClaw（九问）平台集成适配器

> **模块路径**: `agentrt/protocols/integrations/openclaw/` | **版本**: v1.0.0

## 概述

`openclaw/` 是 AgentRT 协议栈的 OpenClaw（九问）开源 AI Agent 平台适配器，专注于政务和企业应用场景的双向桥接集成。实现 AgentRT ↔ OpenClaw 消息互通、工具共享和能力映射，通过 `airy_protocol_interface.h` 和 `unified_protocol.h` 注册到协议路由器。

### 核心能力

| 特性 | 说明 |
|------|------|
| **离线私有化** | 完全本地运行，数据不出域 |
| **安全管控** | 五级安全等级（Public / Internal / Confidential / Secret / TopSecret） |
| **多模态** | 文本/图像/音频/视频/文件/代码统一处理（6 种模态） |
| **多智能体** | Agent 注册/发现/注销，最多 64 个 Agent |
| **工具共享** | 跨 Agent 工具注册与调用（最多 256 个工具） |
| **任务委派** | 任务创建/查询/取消/进度追踪 |
| **会话管理** | 多会话并行（最多 128 个会话），上下文管理 |
| **集群状态** | 节点/Agent/会话/任务统计 |

## 目录结构

```
openclaw/
├── include/
│   └── openclaw_adapter.h           # 适配器接口（模式/安全等级/模态/Agent/工具/会话）
└── src/
    └── openclaw_adapter.c           # 适配器实现
```

## 核心数据结构

### 部署模式

| 模式 | 说明 |
|------|------|
| `OPENCLAW_MODE_STANDALONE` | 单机部署 |
| `OPENCLAW_MODE_CLUSTERED` | 集群部署 |
| `OPENCLAW_MODE_HYBRID` | 混合部署 |
| `OPENCLAW_MODE_EMBEDDED` | 嵌入式部署 |

### 安全等级

| 等级 | 说明 |
|------|------|
| `OPENCLAW_SECURITY_LEVEL_PUBLIC` | 公开 |
| `OPENCLAW_SECURITY_LEVEL_INTERNAL` | 内部 |
| `OPENCLAW_SECURITY_LEVEL_CONFIDENTIAL` | 机密 |
| `OPENCLAW_SECURITY_LEVEL_SECRET` | 秘密 |
| `OPENCLAW_SECURITY_LEVEL_TOP_SECRET` | 绝密 |

### 多模态支持

| 模态 | 标志 | 说明 |
|------|------|------|
| `OPENCLAW_MODALITY_TEXT` | 0x01 | 文本 |
| `OPENCLAW_MODALITY_IMAGE` | 0x02 | 图像 |
| `OPENCLAW_MODALITY_AUDIO` | 0x04 | 音频 |
| `OPENCLAW_MODALITY_VIDEO` | 0x08 | 视频 |
| `OPENCLAW_MODALITY_FILE` | 0x10 | 文件 |
| `OPENCLAW_MODALITY_CODE` | 0x20 | 代码 |
| `OPENCLAW_MODALITY_ALL` | 0x3F | 全部模态 |

### Agent 状态

| 状态 | 说明 |
|------|------|
| `OPENCLAW_AGENT_STATE_IDLE` | 空闲 |
| `OPENCLAW_AGENT_STATE_THINKING` | 思考中 |
| `OPENCLAW_AGENT_STATE_EXECUTING` | 执行中 |

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
| **gateway_d** | 通过协议路由器处理 OpenClaw 平台的入站连接 |
| **market_d** | 通过适配器将 AgentRT 工具注册到 OpenClaw 工具链 |

## 构建

CMake 选项: `PROTOCOLS_ENABLE_OPENCLAW`（默认 OFF，需 Unix sockets）

```bash
cmake -S . -B build -DPROTOCOLS_ENABLE_OPENCLAW=ON
cmake --build build --target airy_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 1.0.0（OpenClaw 九问平台集成适配器）