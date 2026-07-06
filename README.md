**Language:** English | [简体中文](README_zh.md)

# Airymax Protocols — Unified Communication Protocol Stack

`agentrt/protocols/`

**Version:** 0.1.1
**License:** AGPL-3.0-or-later OR Apache-2.0 (dual-licensed)
**Branch:** `feature/official-hubs-01`

---

## 1. Module Positioning

Protocols is the **unified communication protocol stack** of the Airymax agent
runtime. It defines and implements every protocol contract used inside the
system — between modules, between services, and between the runtime and
external platforms. The stack is organized in five layers (Common / Core /
Standards / Integrations / Frameworks) and is compiled into the
`libagentrt_protocols` shared library.

The stack carries three protocol families:

- **AgentsIPC** — the Airymax internal IPC wire format. Its L2 application-level
  message header (`agentrt_ipc_header_t`, defined authoritatively in
  `commons/include/agentrt_types.h`) is a **fixed-length binary header**
  carrying magic, version, type, flags, message ID, correlation ID, 64-byte
  source, 64-byte target, payload length, checksum, and timestamp — the
  canonical envelope for cross-module, cross-service, and application-level
  messaging across Linux / Windows / macOS.
- **A2A (Agent-to-Agent)** — the v0.3 Agent-to-Agent standard protocol adapter
  for inter-agent dialogue and capability exchange.
- **A2T (Agent-to-Tool) / MCP** — the Model Context Protocol (MCP v1.0)
  adapter serves as the Agent-to-Tool contract, exposing tool surfaces to
  agents through a standardized context protocol.

Core design principles:

- **Protocol-agnostic API** — upper-layer business code talks to a unified
  `unified_message_t` model and `protocol_adapter_t` interface; the underlying
  protocol details are irrelevant.
- **Pluggable adapters** — each protocol is an independent adapter that can be
  dynamically registered / unregistered / hot-loaded.
- **Intelligent routing** — a rule-engine-based protocol router performs
  automatic cross-protocol message conversion.
- **Unified registry** — protocol discovery, capability query, dependency
  tracking, and lifecycle management.

---

## 2. Directory Structure

```
protocols/
├── CMakeLists.txt                          # CMake build configuration
├── README.md                               # This file (English)
├── README_zh.md                            # Chinese version
├── LICENSE                                 # Dual license texts (AGPL-3.0 + Apache-2.0)
├── NOTICE                                  # Copyright notice
├── include/                                # Top-level public headers
│   ├── agentrt_protocol_interface.h        # Unified protocol system interface
│   ├── unified_protocol.h                  # Unified message model & protocol types
│   └── protocol_router.h                   # Top-level (lightweight) protocol router
├── src/                                    # Top-level implementation
│   ├── agentrt_protocol_interface.c        # Router / Gateway / Registry unified impl
│   └── protocol_toplevel_impl.c            # Top-level protocol routing impl
├── common/                                 # Common layer — unified protocol interface
│   ├── include/protocols.h                 # Framework main header (init / manager / adapter factory)
│   └── src/
│       ├── unified_protocol.c              # Core (msg create / send / receive / callbacks)
│       └── protocols_impl.c                # Framework init / manager / default adapter / errors
├── core/                                   # Core layer — routing / extension / transform / registry
│   ├── adapter/                            # Extension framework
│   ├── registry/                           # Registry center
│   ├── router/                             # Protocol routing engine
│   └── transformers/                       # Message transformers
├── standards/                              # Standards layer — industry-standard protocols
│   ├── a2a/                                # A2A v0.3 (Agent-to-Agent)
│   ├── mcp/                                # MCP v1.0 (Model Context Protocol — Agent-to-Tool)
│   └── agntcy/                             # AGNTCY ACP
├── integrations/                           # Integrations layer — major AI platform adapters
│   ├── openai/                             # OpenAI API enterprise adapter
│   ├── claude/                             # Anthropic Claude API adapter
│   ├── openjiuwen/                         # OpenJiuwen binary protocol adapter
│   ├── openclaw/                           # OpenClaw (Jiuwen) platform adapter
│   └── china_eco/                          # China ecosystem (Bailian / Wenxin / SM2-4 / OSS)
├── frameworks/                             # Frameworks layer — AI framework integration
│   ├── langchain/                          # LangChain framework adapter
│   └── autogen/                            # AutoGen multi-agent framework adapter
└── tests/                                  # Test suite
    ├── test_openclaw_adapter.c
    ├── test_agntcy_acp.c
    └── test_china_eco_crypto.c
```

### Five-Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Frameworks Layer (framework adapters)            │
│  ┌──────────────────┐  ┌──────────────────┐                        │
│  │ langchain_adapter │  │  autogen_adapter  │                        │
│  └──────────────────┘  └──────────────────┘                        │
├─────────────────────────────────────────────────────────────────────┤
│                  Integrations Layer (platform adapters)              │
│  openai_enterprise | openjiuwen | openclaw | claude | china_eco      │
├─────────────────────────────────────────────────────────────────────┤
│                   Standards Layer (standard protocols)               │
│  a2a_v03_adapter | mcp_v1_adapter | agntcy_acp_adapter              │
├─────────────────────────────────────────────────────────────────────┤
│                     Core Layer (routing / extension / transform)     │
│  protocol_router | protocol_extension_framework | protocol_          │
│                  |                                  |  transformers  │
│  protocol_registry                                                   │
├─────────────────────────────────────────────────────────────────────┤
│                    Common Layer (unified model)                      │
│  unified_protocol.c | protocols_impl.c                              │
└─────────────────────────────────────────────────────────────────────┘
```

### Layer Responsibilities

| Layer | Components | Responsibility |
|-------|-----------|----------------|
| **Common** | `unified_protocol.c`, `protocols_impl.c` | Unified message model (`unified_message_t`), protocol-stack lifecycle (`protocol_stack_*`), adapter registration and routing, framework init / manager / default adapter factory |
| **Core** | `protocol_router.c`, `protocol_extension_framework.c`, `protocol_transformers.c`, `protocol_registry.c` | Routing engine (rule matching / msg conversion), extension framework (plugin adapters / middleware pipeline), message transformers (cross-protocol format adaptation), registry (discovery / capability query / dependency tracking) |
| **Standards** | `a2a_v03_adapter.c`, `mcp_v1_adapter.c`, `mcp_transport.c`, `agntcy_acp_adapter.c` | A2A (Agent-to-Agent), MCP (Model Context Protocol — Agent-to-Tool), AGNTCY ACP — industry-standard protocol adapters |
| **Integrations** | `openai_enterprise_adapter.c`, `claude_adapter.c`, `openjiuwen_adapter.c`, `openclaw_adapter.c`, `china_eco_adapter.c` | OpenAI (Chat / Embeddings / Function Calling / Streaming), Claude (Messages / Tool Use / Extended Thinking / Vision), OpenJiuwen (custom binary protocol), OpenClaw (Jiuwen / multi-agent / safety control), China ecosystem (Bailian / Wenxin / SM2-4 national crypto / object storage) |
| **Frameworks** | `langchain_adapter.c`, `autogen_adapter.c` | LangChain (Chain / Agent / Tool / Memory / RAG / Streaming), AutoGen (multi-agent dialogue / group chat / code execution / human-in-the-loop) |

---

## 3. Upstream / Downstream Dependencies

### Upstream (Protocols depends on)

| Dependency | Source | Purpose |
|------------|--------|---------|
| **commons** | `commons/` | Platform abstraction, memory management, string tools, `agentrt_ipc_header_t` authoritative type definition (the AgentsIPC L2 wire-format header) |
| **atoms/corekern** | `atoms/corekern/` | Kernel type definitions; CoreKern Binder IPC is the underlying transport that the AgentsIPC envelope rides on |
| `svc_common` | `daemons/common/` | Safe-string utilities (`safe_string_utils.c`) |
| `agentrt_compile_defs` | umbrella CMake | Compile definitions |
| cJSON | external | JSON parsing (MCP and other adapters) |
| libcurl | external | HTTP client (some integration adapters) |

### Downstream (consumers of Protocols)

| Consumer | What it uses |
|----------|--------------|
| **gateway** | Gateway uses the protocol router / gateway interfaces to translate HTTP / WS / Stdio into JSON-RPC 2.0 over AgentsIPC, and to bridge A2A / MCP at the protocol boundary |
| **daemons** | All 12 daemons communicate with each other via JSON-RPC 2.0 carried over the AgentsIPC L2 envelope; tool_d / plugin_d expose MCP tool surfaces (Agent-to-Tool) |
| Toolkit / SDK | The SDK ships protocol client libraries built on top of this stack |
| OpenLab applications | All OpenLab modules talk to the core runtime over JSON-RPC 2.0 |

---

## 4. Core Interfaces

### I-L1: Protocol adapter interface (`proto_adapter_vtable_t`)

The unified vtable for every protocol adapter, defining
`init / destroy / encode / decode / connect / disconnect / send / receive / get_stats`
plus capability flags (`proto_capability_flags_t`).

### I-L2: Protocol router interface (`proto_router_iface_t`)

Rule-engine-based router supporting route add / remove, single / batch message
routing, protocol conversion, default-protocol selection, and route statistics.

### I-L3: Protocol gateway interface (`proto_gateway_iface_t`)

Gateway integration interface providing protocol register / unregister, request
handling, automatic protocol detection, event callbacks, and statistics query.

### I-L4: Protocol extension interface (`proto_extension_mgr_iface_t`)

Extension manager interface supporting extension register / unregister,
load / unload, auto-detection, and capability query.

### Usage Example

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

## 5. Build Instructions

The protocols layer builds as a shared library `libagentrt_protocols`. Each
adapter can be individually enabled or disabled through CMake options.

```bash
# Standard build (from the umbrella root, or standalone)
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# Run tests
ctest --test-dir build -R protocols
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF` | OpenClaw (Jiuwen) platform adapter (requires Unix sockets) |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude API adapter |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain framework adapter |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen framework adapter |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP protocol adapter |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` | China ecosystem adapter (requires Unix) |
| `PROTOCOLS_ENABLE_MCP` | `ON` (OFF on Windows) | MCP protocol adapter (requires cJSON / unistd.h) |

Windows note: `OPENCLAW`, `CHINA_ECO`, and `MCP` are force-disabled on Windows
because they require Unix-domain sockets or `unistd.h`.

### Build Artifacts

- `libagentrt_protocols` — shared library aggregating Common / Core / Standards /
  Integrations / Frameworks layers
- Public headers installed under `include/agentrt/protocols`

### Installation

```bash
cmake --install build --prefix /opt/airymax
```

---

## 6. License

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright
notice is in [NOTICE](NOTICE). You may select either license to comply with.
The AGPL-3.0-or-later terms apply by default; the Apache-2.0 alternative is
provided for downstream integration scenarios (e.g., closed-source or
proprietary distribution) that the AGPL does not accommodate.
