# China Eco Adapter — 国内生态协议兼容适配器

> **模块路径**: `agentrt/protocols/integrations/china_eco/` | **版本**: v0.1.0

## 概述

`china_eco/` 是 AgentRT 协议栈的国内生态协议兼容适配器，提供 LLM Provider Bridge、对象存储 Bridge、国密算法和消息队列四大能力，将国内主流 AI 平台和云服务的 API 映射到 AgentRT 统一协议体系。

### 核心能力

| 能力 | 标志 | 说明 |
|------|------|------|
| **LLM Bridge** | `CHINA_ECO_CAP_LLM_BRIDGE` | 百炼/文心/DashScope/智谱/MiniMax/Moonshot/DeepSeek/Qwen 的 OpenAI 兼容层 |
| **Object Storage** | `CHINA_ECO_CAP_OBJECT_STORAGE` | 阿里云 OSS / 腾讯云 COS / 百度云 BOS / 华为云 OBS 统一适配 |
| **SM Crypto** | `CHINA_ECO_CAP_SM_CRYPTO` | SM2 非对称加密 / SM3 哈希 / SM4 对称加密 |
| **Message Queue** | `CHINA_ECO_CAP_MESSAGE_QUEUE` | RocketMQ / Pulsar 消息队列协议映射 |
| **Content Audit** | `CHINA_ECO_CAP_CONTENT_AUDIT` | 内容审核合规 |

## 目录结构

```
china_eco/
├── include/
│   └── china_eco_adapter.h          # 适配器接口（LLM Provider/对象存储/SM加密/消息队列）
└── src/
    └── china_eco_adapter.c          # 适配器实现
```

## 核心数据结构

### LLM Provider 支持

| Provider | 枚举 | 说明 |
|----------|------|------|
| 百炼（Bailian） | `CHINA_ECO_PROVIDER_BAILIAN` | 阿里云百炼平台 |
| 文心（Wenxin） | `CHINA_ECO_PROVIDER_WENXIN` | 百度文心一言 |
| DashScope | `CHINA_ECO_PROVIDER_DASHSCOPE` | 阿里云 DashScope（通义千问） |
| 智谱（Zhipu） | `CHINA_ECO_PROVIDER_ZHIPU` | 智谱 AI（GLM 系列） |
| MiniMax | `CHINA_ECO_PROVIDER_MINIMAX` | MiniMax（ABAB 系列） |
| Moonshot | `CHINA_ECO_PROVIDER_MOONSHOT` | 月之暗面（Kimi） |
| DeepSeek | `CHINA_ECO_PROVIDER_DEEPSEEK` | 深度求索 |
| Qwen | `CHINA_ECO_PROVIDER_QWEN` | 通义千问开源模型 |

### 对象存储支持

| 存储 | 枚举 | 说明 |
|------|------|------|
| 阿里云 OSS | `CHINA_ECO_OSS_ALIYUN` | 阿里云对象存储 |
| 腾讯云 COS | `CHINA_ECO_OSS_TENCENT` | 腾讯云对象存储 |
| 百度云 BOS | `CHINA_ECO_OSS_BAIDU` | 百度云对象存储 |
| 华为云 OBS | `CHINA_ECO_OSS_HUAWEI` | 华为云对象存储 |

### 国密算法

| 算法 | 密钥/哈希大小 | 用途 |
|------|--------------|------|
| SM2 | 64 字节密钥 | 非对称加密 / 数字签名 |
| SM3 | 32 字节哈希 | 密码哈希 |
| SM4 | 16 字节密钥 + 16 字节 IV | 对称加密 |

## 上游依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| **unified_protocol.h** | `protocols/include/` | 统一消息模型 |
| cJSON | 外部 | JSON 解析 |
| libcurl | 外部 | HTTP 客户端 |
| OpenSSL（可选） | 外部 | SM2/SM3/SM4 国密算法 delegate |

## 下游消费者

| 消费者 | 使用方式 |
|--------|----------|
| **gateway_d** | 通过协议路由器将国内 LLM Provider 请求路由到对应平台 |
| **llm_d** | 通过 LLM Bridge 将 LLM 请求分发到国内模型提供商 |
| **tool_d** | 通过对象存储 Bridge 访问国内云存储服务 |

## 构建

CMake 选项: `PROTOCOLS_ENABLE_CHINA_ECO`（默认 ON，Windows 上 OFF）

```bash
cmake -S . -B build -DPROTOCOLS_ENABLE_CHINA_ECO=ON
cmake --build build --target agentrt_protocols
```

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved. 双许可证：AGPL-3.0-or-later OR Apache-2.0。

---

> **文档结束** | 0.1.0（国内生态协议兼容适配器）