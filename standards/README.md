# standards — 开放标准协议适配层

**位置：** `protocols/standards/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`standards/` 收录三个开放标准协议适配器，把行业标准协议映射到
AgentRT 统一协议体系：

| 协议 | 目录 | 适配版本 | 定位 | 说明 |
|------|------|----------|------|------|
| MCP | [`mcp/`](mcp/README.md) | v1.0.0 | Agent ↔ Tool/数据 | Model Context Protocol：工具、资源、提示、采样 |
| A2A | [`a2a/`](a2a/README.md) | v0.3.0 | Agent ↔ Agent | 智能体互访：Agent Card、任务、消息、协商 |
| AGNTCY ACP | [`agntcy/`](agntcy/README.md) | v0.1.0 | Agent ↔ Agent | 智能体连接协议：发现、频道、广播、编排、确认 |

## 目录结构

```
standards/
├── a2a/
│   ├── include/a2a_v03_adapter.h
│   └── src/  a2a_v03_adapter.c · _task.c · _auth.c · _cb.c · _agent.c · _msg.c
├── mcp/
│   ├── include/  mcp_v1_adapter.h · mcp_client.h · mcp_transport.h
│   └── src/  适配器 8 文件 · 客户端 6 文件 · 传输层 1 文件
└── agntcy/
    ├── include/agntcy_acp_adapter.h
    └── src/agntcy_acp_adapter.c
```

完整文件清单见各子目录 README。

## 规模与默认参数对比

| 维度 | MCP v1 | A2A v0.3 | AGNTCY ACP |
|------|--------|----------|------------|
| 默认超时 | 30s | 60s | 30s |
| 消息上限 | 10 MB | 16 MB | 8 MB |
| 容量 | 工具 1024 / 资源 512 / 提示 256 | Agent 256 / 任务 4096 | Agent 512 / 任务 2048 / 频道 256 |
| 安全 | 由承载传输决定 | Token 认证 + AES-256-GCM 签名 | 能力标志 + ACK 协商 |

## 依赖与消费者

上游依赖与 `core/`、`common/` 相同（`unified_protocol.h`、
`airy_protocol_interface.h`、commons）；HTTP/JSON 路径在配置期检测到
cURL、cJSON 时启用。下游主要为 [gateway](https://atomgit.com/openairymax/gateway)：
对外 MCP/A2A 兼容端点由这些适配器与 JSON-RPC 枢纽转换协同完成。

## 构建门控

| 组件 | 门控 |
|------|------|
| A2A 适配器 | 源码无条件编译；`PROTOCOLS_ENABLE_A2A` 为功能标记 |
| MCP v1 适配器 | `PROTOCOLS_ENABLE_MCP`（默认 `ON`） |
| MCP 客户端 | 仅非 Windows 平台编译（POSIX 子进程/管道模型） |
| MCP 传输层 | `PROTOCOLS_ENABLE_MCP_TRANSPORT`（Windows 强制 `OFF`） |
| AGNTCY ACP | `PROTOCOLS_ENABLE_AGNTCY`（默认 `ON`） |

详见[主文档「构建」一节](../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
