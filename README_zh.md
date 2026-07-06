**语言:** [English](README.md) | 简体中文

# Airymax Protocols — 统一通信协议栈

`agentrt/protocols/`

**版本：** 0.1.1
**许可证：** AGPL-3.0-or-later OR Apache-2.0（双许可证）
**分支：** `feature/official-hubs-01`

---

## 1. 模块定位

Protocols 是 Airymax 智能体运行时的**统一通信协议栈**。它定义并实现系统内部
使用的全部协议契约——模块间、服务间、运行时与外部平台之间的通信。协议栈采用
五层架构（Common / Core / Standards / Integrations / Frameworks），编译为
`libagentrt_protocols` 共享库。

协议栈承载三大协议族：

- **AgentsIPC** —— Airymax 内部 IPC 线协议。其 L2 应用级消息头
  （`agentrt_ipc_header_t`，权威定义于 `commons/include/agentrt_types.h`）
  是**定长二进制头**，承载 magic、version、type、flags、消息 ID、关联 ID、
  64 字节 source、64 字节 target、负载长度、校验和、时间戳——是跨模块、
  跨服务、应用级通信的规范信封，跨 Linux / Windows / macOS 兼容。
- **A2A（Agent-to-Agent）** —— v0.3 Agent-to-Agent 标准协议适配器，承载
  Agent 间对话与能力交换。
- **A2T（Agent-to-Tool）/ MCP** —— Model Context Protocol（MCP v1.0）适配器
  作为 Agent-to-Tool 契约，通过标准化上下文协议向 Agent 暴露工具表面。

核心设计理念：

- **协议无关 API** —— 上层业务通过 `unified_message_t` 统一消息模型与
  `protocol_adapter_t` 适配器接口通信，无需关心底层协议细节。
- **可插拔适配器** —— 每种协议实现为独立适配器，支持动态注册/注销/热加载。
- **智能路由** —— 基于规则引擎的协议路由器，自动完成跨协议消息转换。
- **统一注册中心** —— 协议发现、能力查询、依赖追踪、生命周期管理。

---

## 2. 目录结构

```
protocols/
├── CMakeLists.txt                          # CMake 构建配置
├── README.md                               # 英文版
├── README_zh.md                            # 本文件（中文）
├── LICENSE                                 # 双许可证文本（AGPL-3.0 + Apache-2.0）
├── NOTICE                                  # 版权声明
├── include/                                # 顶层公共头文件
│   ├── agentrt_protocol_interface.h        # 协议系统统一接口定义
│   ├── unified_protocol.h                  # 统一消息模型与协议类型
│   └── protocol_router.h                   # 顶层协议路由器（轻量级）
├── src/                                    # 顶层实现
│   ├── agentrt_protocol_interface.c        # Router/Gateway/Registry 统一接口实现
│   └── protocol_toplevel_impl.c            # 顶层协议路由实现
├── common/                                 # 公共层 — 统一协议接口与公共实现
│   ├── include/protocols.h                 # 框架主头文件（初始化/管理器/适配器工厂）
│   └── src/
│       ├── unified_protocol.c              # 协议栈核心（消息创建/发送/接收/回调）
│       └── protocols_impl.c                # 框架初始化/管理器/默认适配器/错误处理
├── core/                                   # 核心层 — 路由/扩展/转换/注册
│   ├── adapter/                            # 扩展框架
│   ├── registry/                           # 注册中心
│   ├── router/                             # 协议路由引擎
│   └── transformers/                       # 消息转换器
├── standards/                              # 标准协议层 — 行业标准协议适配
│   ├── a2a/                                # A2A v0.3 (Agent-to-Agent)
│   ├── mcp/                                # MCP v1.0 (Model Context Protocol — Agent-to-Tool)
│   └── agntcy/                             # AGNTCY ACP
├── integrations/                           # 集成适配层 — 主流 AI 平台集成
│   ├── openai/                             # OpenAI API 企业级适配
│   ├── claude/                             # Anthropic Claude API 适配
│   ├── openjiuwen/                         # OpenJiuwen 二进制协议适配
│   ├── openclaw/                           # OpenClaw (九问) 平台适配
│   └── china_eco/                          # 国内生态（百炼/文心/国密 SM2-4/对象存储）
├── frameworks/                             # 框架适配层 — AI 框架集成
│   ├── langchain/                          # LangChain 框架适配
│   └── autogen/                            # AutoGen 多代理框架适配
└── tests/                                  # 测试套件
    ├── test_openclaw_adapter.c
    ├── test_agntcy_acp.c
    └── test_china_eco_crypto.c
```

### 五层架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Frameworks Layer (框架适配层)                     │
│  ┌──────────────────┐  ┌──────────────────┐                        │
│  │ langchain_adapter │  │  autogen_adapter  │                        │
│  └──────────────────┘  └──────────────────┘                        │
├─────────────────────────────────────────────────────────────────────┤
│                  Integrations Layer (集成适配层)                      │
│  openai_enterprise | openjiuwen | openclaw | claude | china_eco      │
├─────────────────────────────────────────────────────────────────────┤
│                   Standards Layer (标准协议层)                        │
│  a2a_v03_adapter | mcp_v1_adapter | agntcy_acp_adapter              │
├─────────────────────────────────────────────────────────────────────┤
│                     Core Layer (核心层)                              │
│  protocol_router | protocol_extension_framework | protocol_          │
│                  |                                  |  transformers  │
│  protocol_registry                                                   │
├─────────────────────────────────────────────────────────────────────┤
│                    Common Layer (公共层)                             │
│  unified_protocol.c | protocols_impl.c                              │
└─────────────────────────────────────────────────────────────────────┘
```

### 各层职责

| 层次 | 组件 | 职责 |
|------|------|------|
| **Common** | `unified_protocol.c`、`protocols_impl.c` | 统一消息模型 (`unified_message_t`)、协议栈生命周期 (`protocol_stack_*`)、适配器注册与路由、框架初始化/管理器/默认适配器工厂 |
| **Core** | `protocol_router.c`、`protocol_extension_framework.c`、`protocol_transformers.c`、`protocol_registry.c` | 协议路由引擎（规则匹配/消息转换）、扩展框架（插件式适配器管理/中间件管道）、消息转换器（跨协议格式适配）、注册中心（协议发现/能力查询/依赖追踪） |
| **Standards** | `a2a_v03_adapter.c`、`mcp_v1_adapter.c`、`mcp_transport.c`、`agntcy_acp_adapter.c` | A2A（Agent-to-Agent）、MCP（Model Context Protocol，Agent-to-Tool）、AGNTCY ACP 等行业标准协议适配 |
| **Integrations** | `openai_enterprise_adapter.c`、`claude_adapter.c`、`openjiuwen_adapter.c`、`openclaw_adapter.c`、`china_eco_adapter.c` | OpenAI（Chat/Embeddings/Function Calling/Streaming）、Claude（Messages/Tool Use/Extended Thinking/Vision）、OpenJiuwen（自定义二进制协议）、OpenClaw（九问/多智能体/安全管控）、国内生态（百炼/文心/国密 SM2-4/对象存储） |
| **Frameworks** | `langchain_adapter.c`、`autogen_adapter.c` | LangChain（Chain/Agent/Tool/Memory/RAG/Streaming）、AutoGen（多代理对话/群聊/代码执行/人机协作） |

---

## 3. 上游 / 下游依赖关系

### 上游（Protocols 依赖）

| 依赖 | 来源 | 用途 |
|------|------|------|
| **commons** | `commons/` | 平台抽象、内存管理、字符串工具；`agentrt_ipc_header_t` 权威类型定义（AgentsIPC L2 线协议头） |
| **atoms/corekern** | `atoms/corekern/` | 内核类型定义；CoreKern Binder IPC 是 AgentsIPC 信封的底层传输 |
| `svc_common` | `daemons/common/` | 安全字符串工具 (`safe_string_utils.c`) |
| `agentrt_compile_defs` | 伞仓 CMake | 编译定义 |
| cJSON | 外部 | JSON 解析（MCP 等适配器） |
| libcurl | 外部 | HTTP 客户端（部分集成适配器） |

### 下游（消费 Protocols）

| 消费者 | 用途 |
|--------|------|
| **gateway** | 网关通过协议路由器/网关接口将 HTTP/WS/Stdio 转换为 JSON-RPC 2.0 over AgentsIPC，并在协议边界桥接 A2A/MCP |
| **daemons** | 12 个守护进程通过 JSON-RPC 2.0 over AgentsIPC L2 信封相互通信；tool_d / plugin_d 暴露 MCP 工具表面（Agent-to-Tool） |
| Toolkit / SDK | SDK 内置基于本协议栈的协议客户端库 |
| OpenLab 应用 | 所有 OpenLab 模块通过 JSON-RPC 2.0 与核心运行时通信 |

---

## 4. 核心接口

### I-L1：协议适配器接口（`proto_adapter_vtable_t`）

所有协议适配器的统一虚表接口，定义
`init/destroy/encode/decode/connect/disconnect/send/receive/get_stats`
等方法，以及能力标志位 (`proto_capability_flags_t`)。

### I-L2：协议路由接口（`proto_router_iface_t`）

基于规则引擎的协议路由器，支持路由添加/删除、单条/批量消息路由、协议转换、
默认协议设置、路由统计。

### I-L3：协议网关接口（`proto_gateway_iface_t`）

协议网关集成接口，提供协议注册/注销、请求处理、协议自动检测、事件回调、
统计查询。

### I-L4：协议扩展接口（`proto_extension_mgr_iface_t`）

扩展管理器接口，支持扩展注册/注销、加载/卸载、自动检测、能力查询。

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

---

## 5. 构建说明

协议层构建为共享库 `libagentrt_protocols`。每个适配器可通过 CMake 选项
单独启用或禁用。

```bash
# 标准构建（在伞仓根目录或本仓独立构建）
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# 运行测试
ctest --test-dir build -R protocols
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF` | OpenClaw（九问）平台适配器（需 Unix socket） |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude API 适配器 |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain 框架适配器 |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen 框架适配器 |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP 协议适配器 |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` | 国内生态适配器（需 Unix） |
| `PROTOCOLS_ENABLE_MCP` | `ON`（Windows 上为 OFF） | MCP 协议适配器（需 cJSON/unistd.h） |

Windows 注意：`OPENCLAW`、`CHINA_ECO`、`MCP` 在 Windows 上强制禁用，
因为它们依赖 Unix domain socket 或 `unistd.h`。

### 构建产物

- `libagentrt_protocols` —— 聚合 Common / Core / Standards / Integrations /
  Frameworks 五层的共享库
- 公共头文件安装到 `include/agentrt/protocols`

### 安装

```bash
cmake --install build --prefix /opt/airymax
```

---

## 6. 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

本模块采用双许可证，您可以选择以下任一许可证遵守：

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件，版权声明见 [NOTICE](NOTICE)。
默认适用 AGPL-3.0-or-later 条款；Apache-2.0 备选用于 AGPL 无法覆盖的
下游集成场景（如闭源或专有分发）。
