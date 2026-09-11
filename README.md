# protocols — AgentsIPC & A2A/A2T Protocol Stack

> The unified communication contract of the Airymax runtime: every cross-module, cross-service, and external message rides on this stack.
> Leaf repository under the [agentrt](../) management repo.

**Language:** English | [简体中文](README_zh.md)

[![Version](https://img.shields.io/badge/version-0.1.9-5a6b7e)](https://atomgit.com/openairymax/protocols)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **Repository:** `git@atomgit.com:openairymax/protocols.git`
- **Branch:** `develop/hubs-01`
- **Version:** 0.1.9 (aligned with agentrt management repo)

---

## Overview

**protocols** is the **unified communication protocol stack** of the Airymax agent runtime. It defines and implements every protocol contract used inside the system — between modules, between services, and between the runtime and external platforms. The stack is organized in five layers (Common / Core / Standards / Integrations / Frameworks) and is compiled into the `libairy_protocols` shared library.

The stack carries three protocol families:

- **AgentsIPC** — the Airymax internal IPC wire format. Its L2 application-level message header (`airy_ipc_header_t`, defined authoritatively in `commons/include/airy_types.h`) is a **fixed-length binary header** carrying magic, version, type, flags, message ID, correlation ID, 64-byte source, 64-byte target, payload length, checksum, and timestamp — the canonical envelope for cross-module, cross-service, and application-level messaging across Linux / Windows / macOS. Payloads fall into **5 categories** aligned with the runtime's message domains (task / memory / session / telemetry / agent).
- **A2A (Agent-to-Agent)** — the v0.3 Agent-to-Agent standard protocol adapter for inter-agent dialogue and capability exchange.
- **A2T (Agent-to-Tool) / MCP** — the Model Context Protocol (MCP v1.0) adapter serves as the Agent-to-Tool contract, exposing tool surfaces to agents through a standardized context protocol.

Core design principles: protocol-agnostic API (upper-layer code talks to a unified `unified_message_t` model and `protocol_adapter_t` interface), pluggable adapters (each protocol is independently registerable / unregisterable / hot-loadable), intelligent routing (rule-engine-based protocol router performs automatic cross-protocol message conversion), and a unified registry (protocol discovery, capability query, dependency tracking, lifecycle management).

`protocols` is one of the 7 leaf repositories aggregated by the [agentrt](../) management repo, forming the **Protocol Layer** in the cyclic architecture (above the Storage Layer `heapstore`, below the Gateway and Service layers).

## Module Classification

**Class — (Service / Composition layer).**

protocols is neither a foundational primitive (Class A) nor a behavioral safety module (Class B); it is a service/composition module that provides the wire-format contracts and adapter plumbing the rest of the runtime speaks. It depends on `commons` (the authoritative `airy_ipc_header_t` type and platform/string utilities) and `atoms/corekern` (kernel type definitions; CoreKern Binder IPC is the underlying transport that the AgentsIPC envelope rides on). Its consumers — `gateway` and `daemons` — use the protocol router/gateway interfaces to translate transports and bridge A2A/MCP at protocol boundaries.

## Directory Structure

```
protocols/
├── CMakeLists.txt                          # CMake build configuration (shared lib libairy_protocols)
├── README.md                               # This file (English)
├── README_zh.md                            # Chinese version
├── LICENSE                                 # Dual license texts (AGPL-3.0 + Apache-2.0)
├── NOTICE                                  # Copyright notice
├── include/                                # Top-level public headers
│   ├── airy_protocol_interface.h        # Unified protocol system interface
│   ├── unified_protocol.h                  # Unified message model & protocol types
│   └── protocol_router.h                   # Top-level (lightweight) protocol router
├── src/                                    # Top-level implementation
│   ├── airy_protocol_interface.c        # Router / Gateway / Registry unified impl
│   └── protocol_toplevel_impl.c            # Top-level protocol routing impl
├── common/                                 # Common layer — unified protocol interface
│   ├── include/protocols.h                 # Framework main header (init / manager / adapter factory)
│   └── src/
│       ├── unified_protocol.c              # Core (msg create / send / receive / callbacks)
│       └── protocols_impl.c                # Framework init / manager / default adapter / errors
├── core/                                   # Core layer — routing / extension / transform / registry
│   ├── adapter/                            # Extension framework (protocol_extension_framework.h)
│   ├── registry/                           # Registry center (protocol_registry.h)
│   ├── router/                             # Protocol routing engine (protocol_router.h)
│   └── transformers/                       # Message transformers (protocol_transformers.h)
├── standards/                              # Standards layer — industry-standard protocols
│   ├── a2a/                                # A2A v0.3 (Agent-to-Agent) — a2a_v03_adapter.h
│   ├── mcp/                                # MCP v1.0 (Model Context Protocol — Agent-to-Tool) — mcp_v1_adapter.h, mcp_transport.h
│   └── agntcy/                             # AGNTCY ACP — agntcy_acp_adapter.h
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
    ├── test_mcp_adapter.c
    ├── test_a2a_adapter.c
    ├── test_openai_adapter.c
    ├── test_claude_adapter.c
    ├── test_langchain_adapter.c
    ├── test_autogen_adapter.c
    ├── test_openclaw_adapter.c
    ├── test_openjiuwen_adapter.c
    ├── test_agntcy_acp.c
    └── test_china_eco_crypto.c
```

## Core Components

### AgentsIPC — the L2 application-level wire format

The canonical envelope for all cross-module / cross-service messaging, defined authoritatively in `commons/include/airy_types.h`:

```c
typedef struct {
    uint32_t magic;          /* 0x414F5350 = "AOSP" */
    uint32_t version;        /* protocol version */
    uint32_t type;           /* message type */
    uint32_t flags;          /* message flags */
    uint64_t msg_id;         /* message ID */
    uint64_t correlation_id; /* correlation ID (request-response) */
    char     source[64];     /* sender identity */
    char     target[64];     /* target identity */
    uint32_t payload_len;    /* payload length */
    uint32_t checksum;       /* checksum */
    uint64_t timestamp;      /* nanosecond timestamp */
} airy_ipc_header_t;
```

The header carries a structured addressing block (64-byte source + 64-byte target = 128 bytes of routing identity) plus magic/version/type/flags, message & correlation IDs, payload length, checksum, and nanosecond timestamp. **5 payload categories** align with the runtime's message domains:

| Payload category | Domain | Example use |
|------------------|--------|-------------|
| task | Task scheduling | Task creation, cancellation, completion events |
| memory | Memory management | Allocation records, pool stats |
| session | Session lifecycle | Session open/close, context sync |
| telemetry | Observability | Metrics, traces, logs, health |
| agent | Agent runtime | Agent messages, skill invocations, A2A/A2T |

### Three protocol families

| Family | Adapter | Standard | Purpose |
|--------|---------|----------|---------|
| **AgentsIPC** | (L2 envelope, native) | Airymax internal | Cross-module / cross-service messaging on Linux/Windows/macOS |
| **A2A** | `a2a_v03_adapter.c` | A2A v0.3 | Agent-to-Agent dialogue and capability exchange |
| **A2T / MCP** | `mcp_v1_adapter.c`, `mcp_transport.c` | MCP v1.0 | Agent-to-Tool contract; exposes tool surfaces to agents |

### Five-layer architecture

| Layer | Components | Responsibility |
|-------|-----------|----------------|
| **Common** | `unified_protocol.c`, `protocols_impl.c` | Unified message model (`unified_message_t`), protocol-stack lifecycle (`protocol_stack_*`), adapter registration and routing, framework init / manager / default adapter factory |
| **Core** | `protocol_router.c`, `protocol_extension_framework.c`, `protocol_transformers.c`, `protocol_registry.c` | Routing engine (rule matching / msg conversion), extension framework (plugin adapters / middleware pipeline), message transformers (cross-protocol format adaptation), registry (discovery / capability query / dependency tracking) |
| **Standards** | `a2a_v03_adapter.c`, `mcp_v1_adapter.c`, `mcp_transport.c`, `agntcy_acp_adapter.c` | A2A (Agent-to-Agent), MCP (Model Context Protocol — Agent-to-Tool), AGNTCY ACP — industry-standard protocol adapters |
| **Integrations** | `openai_enterprise_adapter.c`, `claude_adapter.c`, `openjiuwen_adapter.c`, `openclaw_adapter.c`, `china_eco_adapter.c` | OpenAI (Chat / Embeddings / Function Calling / Streaming), Claude (Messages / Tool Use / Extended Thinking / Vision), OpenJiuwen (custom binary protocol), OpenClaw (Jiuwen / multi-agent / safety control), China ecosystem (Bailian / Wenxin / SM2-4 national crypto / object storage) |
| **Frameworks** | `langchain_adapter.c`, `autogen_adapter.c` | LangChain (Chain / Agent / Tool / Memory / RAG / Streaming), AutoGen (multi-agent dialogue / group chat / code execution / human-in-the-loop) |

## Architecture

```
┌──────────────────────────────────────────────┐
│             Applications (OpenLab)            │
├──────────────────────────────────────────────┤
│             Ecosystem (Toolkit / SDK)         │
├──────────────────────────────────────────────┤
│              Daemon Services (daemons)        │
├──────────────────────────────────────────────┤
│   Gateway Layer (gateway)                     │
├──────────────────────────────────────────────┤
│          ★ protocols (Protocol Layer) ★      │
├──────────────────────────────────────────────┤
│   Storage (heapstore) / Security (cupolas)    │
├──────────────────────────────────────────────┤
│            atoms / commons / OS               │
└──────────────────────────────────────────────┘

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
│  a2a_v03_adapter (A2A) | mcp_v1_adapter (A2T/MCP) | agntcy_acp      │
├─────────────────────────────────────────────────────────────────────┤
│                     Core Layer (routing / extension / transform)     │
│  protocol_router | protocol_extension_framework | protocol_          │
│                  |                                  |  transformers  │
│  protocol_registry                                                   │
├─────────────────────────────────────────────────────────────────────┤
│                    Common Layer (unified model)                      │
│  unified_protocol.c | protocols_impl.c                              │
└─────────────────────────────────────────────────────────────────────┘
                            │
                            ▼
        AgentsIPC L2 envelope (airy_ipc_header_t + payload)
        rides on atoms/corekern Binder IPC transport
```

## Upstream Dependencies

> `commons` is the foundation for all agentrt modules; protocols consumes it for the authoritative `airy_ipc_header_t` type and platform/string utilities. protocols also depends on `atoms/corekern`.

| Dependency | Source | Purpose |
|------------|--------|---------|
| **commons** | `commons/` | Platform abstraction, memory management, string tools, **authoritative `airy_ipc_header_t` type definition** (the AgentsIPC L2 wire-format header) |
| **atoms/corekern** | `atoms/corekern/` | Kernel type definitions; CoreKern Binder IPC is the underlying transport that the AgentsIPC envelope rides on |
| `svc_common` | `daemons/common/` | Safe-string utilities (`safe_string_utils.c`) |
| `airy_compile_defs` | umbrella CMake | Compile definitions |
| cJSON | external | JSON parsing (MCP and other adapters) |
| libcurl | external | HTTP client (some integration adapters) |

## Downstream Consumers

| Consumer | What they use |
|----------|---------------|
| **gateway** | Gateway uses the protocol router / gateway interfaces to translate HTTP / WS / Stdio into JSON-RPC 2.0 over AgentsIPC, and to bridge A2A / MCP at the protocol boundary |
| **daemons** | All 15 daemons (M4 steady state) communicate with each other via JSON-RPC 2.0 carried over the AgentsIPC L2 envelope; `tool_d` exposes MCP tool surfaces for built-in and plugin tools (Agent-to-Tool) |
| Toolkit / SDK | The SDK ships protocol client libraries built on top of this stack |
| OpenLab applications | All OpenLab modules talk to the core runtime over JSON-RPC 2.0 |

## Build

The protocols layer builds as a shared library `libairy_protocols`. Each adapter can be individually enabled or disabled through CMake options.

```bash
# Standard build (out-of-source, enforced by BAN-33)
cmake -S . -B /tmp/protocols-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build /tmp/protocols-build --parallel $(nproc)

# Run tests
ctest --test-dir /tmp/protocols-build -R protocols --output-on-failure

# Install
cmake --install /tmp/protocols-build --prefix /opt/airymax
```

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `PROTOCOLS_ENABLE_OPENCLAW` | `OFF` | OpenClaw (Jiuwen) platform adapter (requires Unix sockets) |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | Claude API adapter |
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | LangChain framework adapter |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | AutoGen framework adapter |
| `PROTOCOLS_ENABLE_AGNTCY` | `ON` | AGNTCY ACP protocol adapter |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` | China ecosystem adapter (requires Unix) |
| `PROTOCOLS_ENABLE_MCP` | `ON` | MCP protocol adapter (cJSON core cross-platform; Unix socket transport POSIX-only) |
| `PROTOCOLS_ENABLE_MCP_TRANSPORT` | `ON` (OFF on Windows) | MCP Unix socket transport layer (requires POSIX) |

Windows note: `OPENCLAW`, `CHINA_ECO`, and `MCP` are force-disabled on Windows because they require Unix-domain sockets or `unistd.h`.

**Build artifacts:**

- `libairy_protocols` — shared library aggregating Common / Core / Standards / Integrations / Frameworks layers
- Public headers installed under `include/agentrt/protocols`

## API

### Core interfaces

- **I-L1: Protocol adapter interface** (`proto_adapter_vtable_t`) — the unified vtable for every protocol adapter, defining `init / destroy / encode / decode / connect / disconnect / send / receive / get_stats` plus capability flags (`proto_capability_flags_t`).
- **I-L2: Protocol router interface** (`proto_router_iface_t`) — rule-engine-based router supporting route add / remove, single / batch message routing, protocol conversion, default-protocol selection, and route statistics.
- **I-L3: Protocol gateway interface** (`proto_gateway_iface_t`) — gateway integration interface providing protocol register / unregister, request handling, automatic protocol detection, event callbacks, and statistics query.
- **I-L4: Protocol extension interface** (`proto_extension_mgr_iface_t`) — extension manager interface supporting extension register / unregister, load / unload, auto-detection, and capability query.

### Usage example

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

## License

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright notice is in [NOTICE](NOTICE). You may select either license to comply with. The AGPL-3.0-or-later terms apply by default; the Apache-2.0 alternative is provided for downstream integration scenarios (e.g., closed-source or proprietary distribution) that the AGPL does not accommodate.
