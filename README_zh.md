# protocols — AgentRT 统一协议层

> 一套消息模型、一份适配器契约、多种协议：JSON-RPC 2.0、MCP、A2A、OpenAI 兼容 API、Claude、OpenJiuwen、国内生态服务、AGNTCY ACP 与 OpenClaw 统一在一个 C API 之后。

**语言：** [English](README.md) | 简体中文

[![Version](https://img.shields.io/badge/version-0.1.16-5a6b7e)](https://atomgit.com/openairymax/protocols)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **仓库地址：** <https://atomgit.com/openairymax/protocols>

---

## 是什么

**protocols** 是 Airymax 智能体运行时（AgentRT）的协议抽象层，一个以
`airy_protocols` 构建的 C11 库。它把智能体负载所说话的每一种应用层协议
——LLM API、工具协议、智能体互访协议——统一到一套消息模型
（`unified_message_t`）和一份适配器契约（`proto_adapter_vtable_t`）之后，
调用方无需为每种协议写各自的收发与路由代码。

`airy_protocol_type_t` 定义了九种协议类型：

`JSON_RPC`、`MCP`、`A2A`、`OPENAI`、`OPENJIUWEN`、`CLAUDE`、`CHINA_ECO`、
`AGNTCY`、`OPENCLAW`。

JSON-RPC 2.0 是内部枢纽格式：内置转换器在 JSON-RPC 与 MCP、A2A、OpenAI、
OpenJiuwen 之间互转消息，路由器按规则即时套用转换。库内自带 11 个具体
适配器，分三个族——开放标准（`standards/`）、厂商集成
（`integrations/`）、智能体框架（`frameworks/`）。扩展框架与协议注册表
支持第三方适配器在运行时注册、协商版本并加入中间件管线。

本库由 [gateway](https://atomgit.com/openairymax/gateway)
（对外提供 HTTP/WebSocket/SSE/MCP/A2A/OpenAI 兼容端点，向内翻译为
JSON-RPC 2.0）与运行时服务消费，默认构建为静态库。

## 能力

| 能力 | 入口 |
|------|------|
| 覆盖 9 种协议类型的统一消息模型 | `unified_message_t`、`airy_protocol_type_t`、`unified_message_create()` |
| 适配器契约（I-L1）与能力标志 | `proto_adapter_vtable_t`（init/destroy/encode/decode/connect/disconnect/is_connected/send/receive/get_stats/get_name/get_type/get_capabilities）、`proto_capability_flags_t` |
| 协议栈与管理器 | `protocols_framework_init()`、`protocol_manager_*`、`protocol_stack_*`（每管理器最多 32 个栈） |
| 规则路由：单条与批量（I-L2） | `protocol_router_create/add_rule/remove_rule/route/route_batch/set_decision_func/get_stats` |
| 网关集成接口（I-L3） | `proto_gateway_iface_t`、`proto_gateway_standard_create/destroy` |
| 扩展管理器接口（I-L4） | `proto_extension_mgr_iface_t` |
| 协议转换器（JSON-RPC ⇄ MCP/A2A/OpenAI/OpenJiuwen） | `transformer_jsonrpc_to_mcp_request()` … `protocol_auto_transform()`、`protocol_validate_transformed()`、`protocol_list_transformers()` |
| 第三方扩展框架（热加载、中间件链、版本协商） | `proto_ext_register/load/start/add_middleware/negotiate/...`（最多 64 适配器、32 中间件） |
| 协议注册表（发现、依赖、统计、JSON 导出） | `proto_registry_register/find/list_all/activate/heartbeat/get_statistics/export_json`（最多 32 条目） |
| 内置适配器：MCP v1、A2A v0.3、AGNTCY ACP、OpenAI、Claude、OpenClaw、国内生态、OpenJiuwen、LangChain、AutoGen | 各目录自有 API，如 `openjiuwen_adapter_create()`、`proto_registry_initialize_builtins()` |

## 构成

五层结构，自顶向下：

```
protocols/
├── include/            # unified_protocol.h、airy_protocol_interface.h、protocol_router.h
├── src/                # 顶层实现 + 内置接口注册
├── common/             # protocols.h 门面：框架初始化、管理器、协议栈、HTTP 适配器工厂
├── core/
│   ├── adapter/        # 协议扩展框架（描述符、中间件、生命周期）
│   ├── registry/       # 协议注册中心（分类、状态、依赖、统计）
│   ├── router/         # 路由引擎（规则、批量、自定义决策函数）
│   └── transformers/   # JSON-RPC ⇄ MCP/A2A/OpenAI/OpenJiuwen 转换器
├── standards/
│   ├── mcp/            # MCP v1 适配器 + 客户端 + 传输层（STDIO、HTTP+SSE、Streamable HTTP）
│   ├── a2a/            # A2A v0.3（任务、Agent Card、协商、AES-256-GCM 认证）
│   └── agntcy/         # AGNTCY ACP（发现、频道、广播、确认）
├── integrations/
│   ├── openai/         # OpenAI 兼容 chat/embeddings/vision/functions
│   ├── claude/         # Claude messages API（模型、thinking、缓存）
│   ├── openclaw/       # OpenClaw 智能体平台桥接
│   ├── china_eco/      # 国内 LLM 平台、OSS 桥接、SM2/SM3/SM4 国密
│   └── openjiuwen/     # OpenJiuwen 二进制协议
├── frameworks/
│   ├── langchain/      # chains、tools、agents、memory 桥接
│   └── autogen/        # conversable agents、group chats 桥接
└── tests/              # 10 个适配器单元测试可执行文件
```

各目录均有独立 README：
[common](common/README.md) ·
[core](core/README.md) ·
[core/adapter](core/adapter/README.md) ·
[core/registry](core/registry/README.md) ·
[core/router](core/router/README.md) ·
[core/transformers](core/transformers/README.md) ·
[standards](standards/README.md) ·
[standards/mcp](standards/mcp/README.md) ·
[standards/a2a](standards/a2a/README.md) ·
[standards/agntcy](standards/agntcy/README.md) ·
[integrations](integrations/README.md) ·
[integrations/openai](integrations/openai/README.md) ·
[integrations/claude](integrations/claude/README.md) ·
[integrations/openclaw](integrations/openclaw/README.md) ·
[integrations/china_eco](integrations/china_eco/README.md) ·
[integrations/openjiuwen](integrations/openjiuwen/README.md) ·
[frameworks](frameworks/README.md) ·
[frameworks/langchain](frameworks/langchain/README.md) ·
[frameworks/autogen](frameworks/autogen/README.md)

## 用法

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

`protocol_adapter_http()` 是门面中唯一的通用适配器工厂；各协议专用适配器
经由自己的构造函数创建（如 `openjiuwen_adapter_create()`），或通过扩展
框架与注册表注册。消息携带协议、方向（请求/响应/通知/错误）、端点、
payload/body、关联与追踪元数据，由协议栈注册的适配器负责编解码。

## 构建

本模块在 [AgentRT](https://atomgit.com/openairymax/agentrt)
源码树内构建（out-of-source）：

```bash
cmake -S agentrt -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target airy_protocols --parallel

ctest --test-dir build -R protocols --output-on-failure   # 仅非 Windows
cmake --install build --prefix /opt/airymax
```

**CMake 选项：**

| 选项 | 默认值 | 门控对象 |
|------|--------|----------|
| `PROTOCOLS_ENABLE_MCP` | `ON` | MCP v1 适配器源码（8 文件） |
| `PROTOCOLS_ENABLE_MCP_TRANSPORT` | `ON`（Windows 强制 `OFF`） | MCP 传输层（STDIO / HTTP+SSE / Streamable HTTP） |
| `PROTOCOLS_ENABLE_A2A` | `ON` | A2A 适配器功能标记 |
| `PROTOCOLS_ENABLE_OPENAI` | `ON` | OpenAI 适配器功能标记 |
| `PROTOCOLS_ENABLE_OPENJIUWEN` | `ON` | OpenJiuwen 适配器功能标记 |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude 适配器源码 |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP 适配器源码 |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON`（Windows 强制 `OFF`） | 国内生态适配器源码 |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain 适配器源码 |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen 适配器源码 |
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF`（Windows 强制 `OFF`） | OpenClaw 适配器源码 |

对照 `CMakeLists.txt` 实测的说明：

- Common、core、A2A、OpenAI、OpenJiuwen 的源码无条件编译，其选项为功能
  标记；其余适配器族的源码由各自选项门控。
- MCP 客户端源码仅在非 Windows 平台编译（POSIX 子进程/管道模型）。
- `airy_protocols` 链接 `airy_common` 与 `svc_common`，并在配置期检测到
  cURL、cJSON 时启用（`AIRY_HAS_CURL` / `AIRY_HAS_CJSON`）。
- 测试（10 个可执行文件）仅在 `BUILD_TESTS=ON` 且非 Windows 平台构建。

**构建产物：** `airy_protocols`（默认静态库；启用位置无关代码，C11）；
公共头文件安装至 `include/agentrt/protocols`。

## 关系

| 方向 | 模块 | 角色 |
|------|------|------|
| 上游 | [commons](https://atomgit.com/openairymax/commons) | 平台/字符串/同步工具与 `airy_common` 静态库 |
| 上游（可选） | cURL、cJSON | 配置期检测到时启用 HTTP I/O 与 JSON 处理 |
| 下游 | [gateway](https://atomgit.com/openairymax/gateway) | 对外 MCP/A2A/OpenAI 兼容端点，向内 JSON-RPC 2.0 翻译 |
| 下游 | 运行时服务 | 负载使用的 agent/tool/LLM 适配器 |

## 许可证

本模块以以下两种许可证之一双重授权：

- **GNU Affero General Public License v3.0 或更新版本**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件；版权与商标声明见
[NOTICE](NOTICE)。二选一遵循即可。
