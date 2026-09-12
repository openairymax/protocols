# Integrations — 集成适配层

**位置：** `protocols/integrations/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

Integrations 层将外部 AI 平台与生态的专有 API/协议映射到 AgentRT 统一
协议体系。当前包含五个集成方向，每个子目录均为**有真实实现的适配器**
（非仅接口定义），并各自提供统一 vtable（`protocol_adapter_t`）接入
协议栈与路由器。

| 子目录 | 适配对象 | 定位 |
|--------|----------|------|
| [`openai/`](openai/README.md) | OpenAI API 企业级特性 | Chat Completions、Embeddings、流式、限流、多模型路由、审计 |
| [`claude/`](claude/README.md) | Anthropic Claude Messages API | 多轮对话、Tool Use、Extended Thinking、Vision、Prompt Caching |
| [`openjiuwen/`](openjiuwen/README.md) | OpenJiuwen 二进制协议 | 头部 + payload + CRC32 校验尾的消息编解码、连接与心跳管理 |
| [`openclaw/`](openclaw/README.md) | OpenClaw 平台 | Agent 注册/发现、工具共享、任务委派、会话与集群状态（Unix socket） |
| [`china_eco/`](china_eco/README.md) | 国内生态 | LLM Provider 桥接、对象存储桥接、国密 SM3/SM4 |

## 目录结构

```
integrations/
├── openai/        # 1 头 + 8 .c（adapter/model/retry/utils/embed/ctx/chat/cb）
├── claude/        # 1 头 + 5 .c（adapter/proto/http/model/cleanup）
├── openjiuwen/    # 1 头 + 4 .c（adapter/msg/io/net）
├── openclaw/      # 1 头 + 7 .c（adapter/cb/socket/registry/session/monitor/task）
└── china_eco/     # 1 头 + 3 .c（adapter/llm/crypto）
```

各子目录的公开头文件位于 `include/`，实现位于 `src/`（含一个内部
`*_internal.h`）。详细 API 与数据类型见各子目录 README。

## 编译门控

| CMake 选项 | 默认值 | 适用子目录 | 说明 |
|------------|--------|-----------|------|
| —（无条件编译） | 始终 | openai、openjiuwen | 源码始终编译进 `airy_protocols`；对应 `PROTOCOLS_ENABLE_OPENAI` / `PROTOCOLS_ENABLE_OPENJIUWEN` 为功能开关（feature flag） |
| `PROTOCOLS_ENABLE_CLAUDE` | `ON` | claude | 关闭后不参与编译 |
| `PROTOCOLS_ENABLE_OPENCLAW` | **`OFF`** | openclaw | 依赖 Unix domain socket；Windows 上强制 `OFF` |
| `PROTOCOLS_ENABLE_CHINA_ECO` | `ON` | china_eco | Windows 上强制 `OFF` |

构建方式见[主文档「构建」一节](../README_zh.md#构建)。

## 依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| `unified_protocol.h` | `protocols/include/` | 统一消息模型与适配器 vtable |
| libcurl | 外部（`AIRY_HAS_CURL` 探测） | HTTP 类适配器（openai、claude、china_eco）请求发送 |
| cJSON | 外部（`AIRY_HAS_CJSON` 探测） | JSON 编解码 |

openclaw 的 socket 传输为其 `src/` 内 POSIX 辅助实现，不引入额外第三方
依赖；各适配器均可通过各自的 `*_get_adapter()` / 全局接口符号注册进
`core/registry`。

## 相关

- 子目录文档：[openai](openai/README.md) ｜
  [claude](claude/README.md) ｜ [openjiuwen](openjiuwen/README.md) ｜
  [openclaw](openclaw/README.md) ｜ [china_eco](china_eco/README.md)
- 框架类适配（LangChain / AutoGen）见
  [`../frameworks/`](../frameworks/README.md)

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
