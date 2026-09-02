# protocols — AgentsIPC 与 A2A/A2T 协议栈

> Airymax 运行时的统一通信契约：所有跨模块、跨服务、对外消息都搭载于此栈。
> [agentrt](../) 管理仓下的叶子仓。

**语言:** [English](README.md) | 简体中文

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/protocols)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **仓库地址：** `git@atomgit.com:openairymax/protocols.git`
- **分支：** `feature/official-hubs-01`
- **版本：** 0.1.1（Airymax 奠基版本）

---

## 概述

**protocols** 是 Airymax 智能体运行时的**统一通信协议栈**。它定义并实现系统内使用的所有协议契约——模块间、服务间、运行时与外部平台间。协议栈组织为五层（Common / Core / Standards / Integrations / Frameworks），编译为 `libairy_protocols` 共享库。

协议栈承载三大协议族：

- **AgentsIPC** —— Airymax 内部 IPC 线协议。其 L2 应用层消息头（`airy_ipc_header_t`，权威定义于 `commons/include/airy_types.h`）是**定长二进制头**，携带 magic、version、type、flags、消息 ID、关联 ID、64 字节 source、64 字节 target、payload 长度、checksum、timestamp——是跨模块、跨服务、应用层消息在 Linux/Windows/macOS 上的规范信封。Payload 分为 **5 类**，与运行时消息域对齐（task / memory / session / telemetry / agent）。
- **A2A（Agent-to-Agent）** —— v0.3 Agent-to-Agent 标准协议适配器，用于智能体间对话与能力交换。
- **A2T（Agent-to-Tool）/ MCP** —— Model Context Protocol（MCP v1.0）适配器作为 Agent-to-Tool 契约，通过标准化上下文协议向智能体暴露工具接口。

核心设计原则：协议无关 API（上层业务代码面向统一 `unified_message_t` 模型和 `protocol_adapter_t` 接口）、可拔插适配器（每个协议可独立注册/注销/热加载）、智能路由（基于规则引擎的协议路由器自动跨协议消息转换）、统一注册中心（协议发现、能力查询、依赖追踪、生命周期管理）。

在 Airymax 0.1.1 发行版中，工作区被拆分为 **38 个仓库**（1 umbrella + 5 management + 29 leaf + 3 top-level）；`protocols` 是 [agentrt](../) 管理仓聚合的 7 个叶子仓之一，构成循环架构中的**协议层**（位于存储层 `heapstore` 之上，网关层与服务层之下）。

## 模块分类

**类 ——（服务 / 组合层）。**

protocols 既非基础原语（A 类），也非行为安全模块（B 类）；它是服务/组合模块，提供运行时其余部分所使用的线协议契约和适配器管道。它依赖 `commons`（权威 `airy_ipc_header_t` 类型和平台/字符串工具）和 `atoms/corekern`（内核类型定义；CoreKern Binder IPC 是 AgentsIPC 信封搭载的底层传输）。其消费者——`gateway` 和 `daemons`——使用协议路由器/网关接口翻译传输并在协议边界桥接 A2A/MCP。

## 目录结构

```
protocols/
├── CMakeLists.txt                          # CMake 构建配置（共享库 libairy_protocols）
├── README.md                               # 英文版
├── README_zh.md                            # 本文件（中文）
├── LICENSE                                 # 双许可证文本（AGPL-3.0 + Apache-2.0）
├── NOTICE                                  # 版权声明
├── include/                                # 顶层公共头
│   ├── airy_protocol_interface.h        # 统一协议系统接口
│   ├── unified_protocol.h                  # 统一消息模型与协议类型
│   └── protocol_router.h                   # 顶层（轻量）协议路由器
├── src/                                    # 顶层实现
│   ├── airy_protocol_interface.c        # 路由器/网关/注册中心统一实现
│   └── protocol_toplevel_impl.c            # 顶层协议路由实现
├── common/                                 # Common 层——统一协议接口
│   ├── include/protocols.h                 # 框架主头（init / manager / adapter factory）
│   └── src/
│       ├── unified_protocol.c              # 核心（msg 创建/发送/接收/回调）
│       └── protocols_impl.c                # 框架 init / manager / 默认适配器 / 错误
├── core/                                   # Core 层——路由/扩展/转换/注册
│   ├── adapter/                            # 扩展框架（protocol_extension_framework.h）
│   ├── registry/                           # 注册中心（protocol_registry.h）
│   ├── router/                             # 协议路由引擎（protocol_router.h）
│   └── transformers/                       # 消息转换器（protocol_transformers.h）
├── standards/                              # Standards 层——行业标准协议
│   ├── a2a/                                # A2A v0.3（Agent-to-Agent）—— a2a_v03_adapter.h
│   ├── mcp/                                # MCP v1.0（Model Context Protocol——Agent-to-Tool）—— mcp_v1_adapter.h、mcp_transport.h
│   └── agntcy/                             # AGNTCY ACP —— agntcy_acp_adapter.h
├── integrations/                           # Integrations 层——主流 AI 平台适配器
│   ├── openai/                             # OpenAI API 企业适配器
│   ├── claude/                             # Anthropic Claude API 适配器
│   ├── openjiuwen/                         # OpenJiuwen 二进制协议适配器
│   ├── openclaw/                           # OpenClaw（Jiuwen）平台适配器
│   └── china_eco/                          # 中国生态（百炼/文心/SM2-4 国密/OSS）
├── frameworks/                             # Frameworks 层——AI 框架集成
│   ├── langchain/                          # LangChain 框架适配器
│   └── autogen/                            # AutoGen 多智能体框架适配器
└── tests/                                  # 测试套件
    ├── test_openclaw_adapter.c
    ├── test_agntcy_acp.c
    └── test_china_eco_crypto.c
```

## 核心组件

### AgentsIPC —— L2 应用层线协议

所有跨模块/跨服务消息的规范信封，权威定义于 `commons/include/airy_types.h`：

```c
typedef struct {
    uint32_t magic;          /* 0x414F5350 = "AOSP" */
    uint32_t version;        /* 协议版本 */
    uint32_t type;           /* 消息类型 */
    uint32_t flags;          /* 消息标志 */
    uint64_t msg_id;         /* 消息 ID */
    uint64_t correlation_id; /* 关联 ID（请求-响应） */
    char     source[64];     /* 发送者标识 */
    char     target[64];     /* 目标标识 */
    uint32_t payload_len;    /* 负载长度 */
    uint32_t checksum;       /* 校验和 */
    uint64_t timestamp;      /* 纳秒时间戳 */
} airy_ipc_header_t;
```

头部携带结构化寻址块（64 字节 source + 64 字节 target = 128 字节路由标识）加 magic/version/type/flags、消息与关联 ID、payload 长度、checksum、纳秒时间戳。**5 类 payload** 与运行时消息域对齐：

| Payload 类别 | 域 | 示例用途 |
|-------------|-----|---------|
| task | 任务调度 | 任务创建、取消、完成事件 |
| memory | 内存管理 | 分配记录、池统计、arena 操作 |
| session | 会话生命周期 | 会话开启/关闭、上下文同步 |
| telemetry | 可观测性 | 指标、追踪、日志、健康 |
| agent | 智能体运行时 | 智能体消息、技能调用、A2A/A2T |

### 三大协议族

| 协议族 | 适配器 | 标准 | 用途 |
|--------|--------|------|------|
| **AgentsIPC** | （L2 信封，原生） | Airymax 内部 | Linux/Windows/macOS 上跨模块/跨服务消息 |
| **A2A** | `a2a_v03_adapter.c` | A2A v0.3 | 智能体间对话与能力交换 |
| **A2T / MCP** | `mcp_v1_adapter.c`、`mcp_transport.c` | MCP v1.0 | Agent-to-Tool 契约；向智能体暴露工具接口 |

### 五层架构

| 层 | 组件 | 职责 |
|----|------|------|
| **Common** | `unified_protocol.c`、`protocols_impl.c` | 统一消息模型（`unified_message_t`）、协议栈生命周期（`protocol_stack_*`）、适配器注册与路由、框架 init / manager / 默认适配器工厂 |
| **Core** | `protocol_router.c`、`protocol_extension_framework.c`、`protocol_transformers.c`、`protocol_registry.c` | 路由引擎（规则匹配/消息转换）、扩展框架（插件适配器/中间件管道）、消息转换器（跨协议格式适配）、注册中心（发现/能力查询/依赖追踪） |
| **Standards** | `a2a_v03_adapter.c`、`mcp_v1_adapter.c`、`mcp_transport.c`、`agntcy_acp_adapter.c` | A2A（Agent-to-Agent）、MCP（Model Context Protocol——Agent-to-Tool）、AGNTCY ACP——行业标准协议适配器 |
| **Integrations** | `openai_enterprise_adapter.c`、`claude_adapter.c`、`openjiuwen_adapter.c`、`openclaw_adapter.c`、`china_eco_adapter.c` | OpenAI（Chat / Embeddings / Function Calling / Streaming）、Claude（Messages / Tool Use / Extended Thinking / Vision）、OpenJiuwen（自定义二进制协议）、OpenClaw（Jiuwen / 多智能体 / 安全控制）、中国生态（百炼/文心/SM2-4 国密/对象存储） |
| **Frameworks** | `langchain_adapter.c`、`autogen_adapter.c` | LangChain（Chain / Agent / Tool / Memory / RAG / Streaming）、AutoGen（多智能体对话 / 群聊 / 代码执行 / 人在回路） |

## 架构

```
┌──────────────────────────────────────────────┐
│             Applications (OpenLab)            │
├──────────────────────────────────────────────┤
│             Ecosystem (Toolkit / SDK)         │
├──────────────────────────────────────────────┤
│              守护进程服务（daemons）            │
├──────────────────────────────────────────────┤
│   网关层（gateway）                            │
├──────────────────────────────────────────────┤
│          ★ protocols（协议层） ★              │
├──────────────────────────────────────────────┤
│   存储（heapstore）/ 安全（cupolas）           │
├──────────────────────────────────────────────┤
│            atoms / commons / OS               │
└──────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                     Frameworks 层（框架适配器）                       │
│  ┌──────────────────┐  ┌──────────────────┐                        │
│  │ langchain_adapter │  │  autogen_adapter  │                        │
│  └──────────────────┘  └──────────────────┘                        │
├─────────────────────────────────────────────────────────────────────┤
│                  Integrations 层（平台适配器）                        │
│  openai_enterprise | openjiuwen | openclaw | claude | china_eco      │
├─────────────────────────────────────────────────────────────────────┤
│                   Standards 层（标准协议）                            │
│  a2a_v03_adapter (A2A) | mcp_v1_adapter (A2T/MCP) | agntcy_acp      │
├─────────────────────────────────────────────────────────────────────┤
│                     Core 层（路由/扩展/转换）                         │
│  protocol_router | protocol_extension_framework | protocol_          │
│                  |                                  |  transformers  │
│  protocol_registry                                                   │
├─────────────────────────────────────────────────────────────────────┤
│                    Common 层（统一模型）                              │
│  unified_protocol.c | protocols_impl.c                              │
└─────────────────────────────────────────────────────────────────────┘
                            │
                            ▼
        AgentsIPC L2 信封（airy_ipc_header_t + payload）
        搭载于 atoms/corekern Binder IPC 传输之上
```

## 上游依赖

> `commons` 是所有 agentrt 模块的基础库；protocols 消费它以获取权威 `airy_ipc_header_t` 类型和平台/字符串工具。protocols 还依赖 `atoms/corekern`。

| 依赖 | 来源 | 用途 |
|------|------|------|
| **commons** | `commons/` | 平台抽象、内存管理、字符串工具、**权威 `airy_ipc_header_t` 类型定义**（AgentsIPC L2 线协议头） |
| **atoms/corekern** | `atoms/corekern/` | 内核类型定义；CoreKern Binder IPC 是 AgentsIPC 信封搭载的底层传输 |
| `svc_common` | `daemons/common/` | 安全字符串工具（`safe_string_utils.c`） |
| `airy_compile_defs` | 伞仓 CMake | 编译宏 |
| cJSON | 外部 | JSON 解析（MCP 等适配器） |
| libcurl | 外部 | HTTP 客户端（部分集成适配器） |

## 下游消费者

| 消费者 | 用途 |
|--------|------|
| **gateway** | 网关使用协议路由器/网关接口将 HTTP/WS/Stdio 翻译为基于 AgentsIPC 的 JSON-RPC 2.0，并在协议边界桥接 A2A/MCP |
| **daemons** | 全部 15 个守护进程（M4 整编稳态）通过搭载于 AgentsIPC L2 信封的 JSON-RPC 2.0 相互通信；`tool_d` 对内建与插件工具暴露 MCP 工具接口（Agent-to-Tool） |
| Toolkit / SDK | SDK 附带基于此栈构建的协议客户端库 |
| OpenLab 应用 | 所有 OpenLab 模块通过 JSON-RPC 2.0 与核心运行时通信 |

## 构建

协议层构建为共享库 `libairy_protocols`。每个适配器可通过 CMake 选项单独启用/禁用。

```bash
# 标准构建（源外构建，BAN-33 强制要求）
cmake -S . -B /tmp/protocols-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build /tmp/protocols-build --parallel $(nproc)

# 运行测试
ctest --test-dir /tmp/protocols-build -R protocols --output-on-failure

# 安装
cmake --install /tmp/protocols-build --prefix /opt/airymax
```

**CMake 选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF` | OpenClaw（Jiuwen）平台适配器（需 Unix socket） |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude API 适配器 |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain 框架适配器 |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen 框架适配器 |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP 协议适配器 |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` | 中国生态适配器（需 Unix） |
| `PROTOCOLS_ENABLE_MCP` | `ON`（Windows 上 OFF） | MCP 协议适配器（需 cJSON / unistd.h） |

Windows 注意：`OPENCLAW`、`CHINA_ECO`、`MCP` 在 Windows 上强制禁用，因为它们需要 Unix-domain socket 或 `unistd.h`。

**构建产物：**

- `libairy_protocols` —— 聚合 Common / Core / Standards / Integrations / Frameworks 层的共享库
- 公共头文件安装到 `include/agentrt/protocols`

## API

### 核心接口

- **I-L1：协议适配器接口**（`proto_adapter_vtable_t`）—— 每个协议适配器的统一 vtable，定义 `init / destroy / encode / decode / connect / disconnect / send / receive / get_stats` 及能力标志（`proto_capability_flags_t`）。
- **I-L2：协议路由器接口**（`proto_router_iface_t`）—— 基于规则引擎的路由器，支持路由增删、单条/批量消息路由、协议转换、默认协议选择、路由统计。
- **I-L3：协议网关接口**（`proto_gateway_iface_t`）—— 网关集成接口，提供协议注册/注销、请求处理、自动协议检测、事件回调、统计查询。
- **I-L4：协议扩展接口**（`proto_extension_mgr_iface_t`）—— 扩展管理器接口，支持扩展注册/注销、加载/卸载、自动检测、能力查询。

### 使用示例

```c
#include "protocols.h"

protocols_framework_init();
protocol_manager_handle_t mgr = protocol_manager_create();

protocol_stack_config_t cfg = protocol_stack_config_default("main-stack");
protocol_stack_handle_t stack = protocol_manager_create_stack(mgr, &cfg);

protocol_adapter_t http_adapter = *protocol_adapter_http();
protocol_stack_register_adapter(stack, http_adapter);

unified_message_t msg = unified_message_create(
    PROTOCOL_HTTP, DIRECTION_REQUEST, "/api/v1/chat", payload, payload_size);
protocol_stack_send(stack, &msg);

protocol_manager_destroy_stack(mgr, stack);
protocol_manager_destroy(mgr);
protocols_framework_cleanup();
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

本模块采用双许可证，您可以选择以下任一许可证遵守：

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件；版权声明见 [NOTICE](NOTICE)。默认适用 AGPL-3.0-or-later 条款；Apache-2.0 备选用于 AGPL 无法覆盖的下游集成场景（如闭源或专有分发）。
