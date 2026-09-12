# mcp — Model Context Protocol 适配三件套

**位置：** `protocols/standards/mcp/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

`mcp/` 目录包含三个相互衔接的组件：

1. **MCP v1 适配器**（`mcp_v1_*`）：服务端角色——对外暴露工具、资源、
   提示模板，并处理 `tools/call`、`sampling/createMessage` 等请求；
2. **MCP 客户端**（`mcp_client_*`）：客户端角色——连接并消费**外部**
   MCP server 的工具（stdio 子进程或 HTTP 两种方式）；
3. **传输抽象层**（`mcp_transport_*`）：为适配器提供统一的
   STDIO / HTTP+SSE / Streamable HTTP 消息通道。

## 目录结构

```
mcp/
├── include/
│   ├── mcp_v1_adapter.h       # 适配器接口
│   ├── mcp_client.h           # 客户端接口
│   └── mcp_transport.h        # 传输抽象接口
├── src/
│   ├── mcp_v1_adapter.c · mcp_v1_adapter_cb.c · mcp_v1_adapter_route.c
│   ├── mcp_v1_adapter_prompt.c · mcp_v1_adapter_sampling.c
│   ├── mcp_v1_adapter_tool.c · mcp_v1_adapter_stream.c · mcp_v1_adapter_resource.c
│   ├── mcp_client.c · mcp_client_tool.c · mcp_client_rpc.c
│   ├── mcp_client_http.c · mcp_client_frame.c · mcp_client_stdio.c
│   └── mcp_transport.c
└── README.md
```

## MCP v1 适配器

### 常量与规模

| 常量 | 值 |
|------|-----|
| `MCP_V1_VERSION` | `"1.0.0"` |
| 工具 / 资源 / 提示上限 | 1024 / 512 / 256 |
| 单工具参数上限 / Schema 嵌套深度 | 32 / 16 |
| 默认超时 / 消息上限 | 30s / 10 MB |

能力标志 6 项（工具、资源、提示、采样、日志、补全）；内容类型 4 种
（text / image / resource / embedded）；日志级别 8 级（对齐 syslog，
`MCP_LOG_DEBUG` … `MCP_LOG_EMERGENCY`）。

### API 摘要

| 分组 | 函数 |
|------|------|
| 生命周期 | `mcp_v1_config_default()`、`mcp_v1_context_create(config)` / `mcp_v1_context_destroy()` |
| 注册 | `mcp_v1_register_tool()` / `register_resource()` / `register_resource_template()` / `register_prompt()` |
| 回调挂接 | `mcp_v1_set_sampling_handler()` / `set_completion_handler()` / `set_progress_callback()` / `set_log_callback()` / `set_log_level()` |
| 请求处理 | `mcp_v1_handle_tools_list()` / `tools_call()` / `resources_list()` / `resources_read()` / `resources_templates()` / `prompts_list()` / `prompts_get()` / `sampling()` / `completion()` |
| 流式 | `mcp_v1_stream_config()` / `handle_tools_call_streaming()` / `handle_sampling_streaming()`、`mcp_stream_event_init()` / `mcp_stream_event_type_string()` |
| 通知 | `mcp_v1_send_progress()`、`mcp_v1_notify_cancelled()` |
| 统一入口 | `mcp_v1_route_request(method, params_json, ...)` |
| 查询 | `mcp_v1_get_adapter()`（统一适配器接口）、`get_tool/resource/prompt_count()`、`get_capabilities()` |
| 传输绑定 | `mcp_v1_set_transport(ctx, transport)` / `mcp_v1_get_transport()` |
| 释放 | `mcp_content_destroy()`、`mcp_sampling_result_destroy()`、`mcp_completion_result_destroy()` |

响应以 JSON 字符串返回（`char **response_json`），由调用方释放。

## MCP 客户端

面向「gateway 消费外部 MCP server 工具」的场景。客户端协议版本为
MCP `"2024-11-05"`，默认超时 60s，消息上限 10 MB。

| 传输 | 枚举值 | 机制 |
|------|--------|------|
| stdio | `MCP_CLIENT_TRANSPORT_STDIO` | fork + exec 外部 server 进程，stdin/stdout 承载 JSON-RPC 2.0；LSP 风格 `Content-Length` 帧，状态机处理粘帧/拆帧 |
| http | `MCP_CLIENT_TRANSPORT_HTTP` | 基本 Streamable HTTP：每请求一次 POST，`Accept: application/json, text/event-stream` |

另有 `MCP_CLIENT_TRANSPORT_NONE`（未连接）。API：

| 函数 | 说明 |
|------|------|
| `mcp_client_connect_stdio(name, command, argv[])` | 拉起外部 MCP server 并握手 |
| `mcp_client_connect_http(name, url)` | 连接 HTTP 端点的 MCP server |
| `mcp_client_disconnect()` | 断开连接 |
| `mcp_client_list_tools()` | 枚举远端工具 |
| `mcp_client_call_tool()` | 调用远端工具 |
| `mcp_client_extract_text()` | 从工具结果提取文本内容 |
| `mcp_client_tool_list_free()` | 释放工具列表 |
| `mcp_client_transport_string()` | 传输类型转字符串 |

错误码基于 `-2000`（进程退出、RPC 错误、连接失败、未连接、帧错误等）。

> 客户端源码使用 POSIX 子进程/管道模型，**仅在非 Windows 平台编译**。

## 传输抽象层

| 类型枚举 | 值 |
|----------|-----|
| `MCP_TRANSPORT_STDIO` | 0（配置 `input_fd` / `output_fd`） |
| `MCP_TRANSPORT_HTTP_SSE` | 1 |
| `MCP_TRANSPORT_STREAMABLE_HTTP` | 2 |

HTTP 配置含 `base_url`、`api_key`、`sse_endpoint`、`post_endpoint`、
重连间隔与最大重连次数。状态机：`DISCONNECTED` / `CONNECTING` /
`CONNECTED` / `ERROR`。API：`mcp_transport_config_stdio_default()`、
`mcp_transport_config_http_default(base_url)`、`create()` / `destroy()`、
`start()` / `stop()`、`send()` / `receive()`、
`mcp_transport_state_string()` / `type_string()`。

> 传输层由 `PROTOCOLS_ENABLE_MCP_TRANSPORT` 门控，Windows 平台强制关闭。

## 构建门控

| 组件 | CMake 条件 |
|------|-----------|
| MCP v1 适配器（8 文件） | `PROTOCOLS_ENABLE_MCP`（默认 `ON`） |
| MCP 客户端（6 文件） | 非 Windows 平台 |
| MCP 传输层（1 文件） | `PROTOCOLS_ENABLE_MCP_TRANSPORT`（默认 `ON`，Windows 强制 `OFF`） |

`tests/test_mcp_adapter.c` 覆盖适配器行为（仅 `BUILD_TESTS=ON` 且非 Windows）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
