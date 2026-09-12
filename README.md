# protocols — AgentRT Unified Protocol Layer

> One message model, one adapter contract, many protocols: JSON-RPC 2.0, MCP, A2A, OpenAI-compatible APIs, Claude, OpenJiuwen, China-ecosystem services, AGNTCY ACP, and OpenClaw behind a single C API.

**Language:** English | [简体中文](README_zh.md)

[![Version](https://img.shields.io/badge/version-0.1.15-5a6b7e)](https://atomgit.com/openairymax/protocols)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **Repository:** <https://atomgit.com/openairymax/protocols>

---

## What It Is

**protocols** is the protocol abstraction layer of the Airymax agent runtime
(AgentRT), a C11 library built as `airy_protocols`. It puts every
application-layer protocol an agent workload speaks — LLM APIs, tool
protocols, agent-to-agent protocols — behind one unified message model
(`unified_message_t`) and one adapter contract (`proto_adapter_vtable_t`), so
callers route, send, and receive without per-protocol code.

Nine protocol types are defined in `airy_protocol_type_t`:

`JSON_RPC`, `MCP`, `A2A`, `OPENAI`, `OPENJIUWEN`, `CLAUDE`, `CHINA_ECO`,
`AGNTCY`, `OPENCLAW`.

JSON-RPC 2.0 is the internal hub format: the built-in transformers convert
messages between JSON-RPC and MCP, A2A, OpenAI, and OpenJiuwen, and the router
applies rule-based conversion on the fly. Eleven concrete adapters ship in
three families — open standards (`standards/`), vendor integrations
(`integrations/`), and agent frameworks (`frameworks/`). The extension
framework and the protocol registry let third-party adapters register,
negotiate versions, and join the middleware pipeline at runtime.

The library is consumed by the [gateway](https://atomgit.com/openairymax/gateway)
(external HTTP/WebSocket/SSE/MCP/A2A/OpenAI-compatible endpoints translated to
internal JSON-RPC 2.0) and by runtime services. It builds as a static library
by default.

## Capabilities

| Capability | Entry points |
|------------|--------------|
| Unified message model across 9 protocol types | `unified_message_t`, `airy_protocol_type_t`, `unified_message_create()` |
| Adapter contract (I-L1) with capability flags | `proto_adapter_vtable_t` (init/destroy/encode/decode/connect/disconnect/is_connected/send/receive/get_stats/get_name/get_type/get_capabilities), `proto_capability_flags_t` |
| Protocol stacks and manager | `protocols_framework_init()`, `protocol_manager_*`, `protocol_stack_*` (up to 32 stacks per manager) |
| Rule-based routing, single & batch (I-L2) | `protocol_router_create/add_rule/remove_rule/route/route_batch/set_decision_func/get_stats` |
| Gateway integration interface (I-L3) | `proto_gateway_iface_t`, `proto_gateway_standard_create/destroy` |
| Extension manager interface (I-L4) | `proto_extension_mgr_iface_t` |
| Protocol transformers (JSON-RPC ⇄ MCP/A2A/OpenAI/OpenJiuwen) | `transformer_jsonrpc_to_mcp_request()` … `protocol_auto_transform()`, `protocol_validate_transformed()`, `protocol_list_transformers()` |
| Third-party extension framework (hot load, middleware chain, version negotiation) | `proto_ext_register/load/start/add_middleware/negotiate/...` (up to 64 adapters, 32 middleware) |
| Protocol registry (discovery, dependencies, stats, JSON export) | `proto_registry_register/find/list_all/activate/heartbeat/get_statistics/export_json` (up to 32 entries) |
| Built-in adapters: MCP v1, A2A v0.3, AGNTCY ACP, OpenAI, Claude, OpenClaw, China eco, OpenJiuwen, LangChain, AutoGen | per-directory APIs, e.g. `openjiuwen_adapter_create()`, `proto_registry_initialize_builtins()` |

## Composition

Five layers, top of the stack first:

```
protocols/
├── include/            # unified_protocol.h, airy_protocol_interface.h, protocol_router.h
├── src/                # toplevel implementation + builtin interface registration
├── common/             # protocols.h facade: framework init, manager, stacks, HTTP adapter factory
├── core/
│   ├── adapter/        # protocol extension framework (descriptors, middleware, lifecycle)
│   ├── registry/       # protocol registry center (categories, states, dependencies, stats)
│   ├── router/         # routing engine (rules, batch, custom decision functions)
│   └── transformers/   # JSON-RPC ⇄ MCP/A2A/OpenAI/OpenJiuwen converters
├── standards/
│   ├── mcp/            # MCP v1 adapter + client + transport (STDIO, HTTP+SSE, Streamable HTTP)
│   ├── a2a/            # A2A v0.3 (tasks, agent cards, negotiation, AES-256-GCM auth)
│   └── agntcy/         # AGNTCY ACP (discovery, channels, broadcast, ack)
├── integrations/
│   ├── openai/         # OpenAI-compatible chat/embeddings/vision functions
│   ├── claude/         # Claude messages API (models, thinking, caching)
│   ├── openclaw/       # OpenClaw agent platform bridge
│   ├── china_eco/      # China LLM platforms, OSS bridges, SM2/SM3/SM4 crypto
│   └── openjiuwen/     # OpenJiuwen binary protocol
├── frameworks/
│   ├── langchain/      # chains, tools, agents, memory bridge
│   └── autogen/        # conversable agents, group chats bridge
└── tests/              # 10 adapter unit-test executables
```

Each directory has its own README:
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

## Usage

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

`protocol_adapter_http()` is the only generic adapter factory in the facade;
protocol-specific adapters are created through their own constructors (for
example `openjiuwen_adapter_create()`) or registered via the extension
framework and the registry. Messages carry protocol, direction
(request/response/notification/error), endpoint, payload/body, correlation and
trace metadata, and are encoded/decoded by whichever adapter the stack has
registered.

## Build

The module builds as part of an [AgentRT](https://atomgit.com/openairymax/agentrt)
source tree (out-of-source):

```bash
cmake -S agentrt -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target airy_protocols --parallel

ctest --test-dir build -R protocols --output-on-failure   # non-Windows only
cmake --install build --prefix /opt/airymax
```

**CMake options:**

| Option | Default | Gates |
|--------|---------|-------|
| `PROTOCOLS_ENABLE_MCP` | `ON` | MCP v1 adapter sources (8 files) |
| `PROTOCOLS_ENABLE_MCP_TRANSPORT` | `ON` (forced `OFF` on Windows) | MCP transport layer (STDIO / HTTP+SSE / Streamable HTTP) |
| `PROTOCOLS_ENABLE_A2A` | `ON` | flag for the A2A adapter |
| `PROTOCOLS_ENABLE_OPENAI` | `ON` | flag for the OpenAI adapter |
| `PROTOCOLS_ENABLE_OPENJIUWEN` | `ON` | flag for the OpenJiuwen adapter |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude adapter sources |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP adapter sources |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` (forced `OFF` on Windows) | China-ecosystem adapter sources |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain adapter sources |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen adapter sources |
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF` (forced `OFF` on Windows) | OpenClaw adapter sources |

Notes measured against `CMakeLists.txt`:

- Common, core, A2A, OpenAI, and OpenJiuwen sources are compiled
  unconditionally; their options act as feature flags. The remaining adapter
  families are source-gated by their option.
- The MCP client sources build only on non-Windows platforms (POSIX
  subprocess/pipe model).
- `airy_protocols` links `airy_common` and `svc_common`, and picks up cURL and
  cJSON when detected (`AIRY_HAS_CURL` / `AIRY_HAS_CJSON`).
- Tests (10 executables) are built with `BUILD_TESTS=ON` on non-Windows
  platforms only.

**Artifacts:** `airy_protocols` (static by default; position-independent code
enabled, C11); public headers install under `include/agentrt/protocols`.

## Relationships

| Side | Module | Role |
|------|--------|------|
| Upstream | [commons](https://atomgit.com/openairymax/commons) | platform/string/sync utilities and the `airy_common` static library |
| Upstream (optional) | cURL, cJSON | HTTP I/O and JSON handling when detected at configure time |
| Downstream | [gateway](https://atomgit.com/openairymax/gateway) | external MCP/A2A/OpenAI-compatible endpoints, internal JSON-RPC 2.0 translation |
| Downstream | runtime services | agent/tool/LLM adapters used by workloads |

## License

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright and
trademark notice is in [NOTICE](NOTICE). You may select either license to
comply with.
