# core — 核心引擎层

**位置：** `protocols/core/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`core/` 提供协议层的四个引擎组件：路由（router）、消息转换
（transformers）、扩展框架（adapter）与注册中心（registry）。
它们不直接对接任何外部协议端点，而是为上层适配器族与网关提供
跨协议路由、格式互转、第三方扩展管理与协议发现能力。

四个组件的源码均无条件编译进 `airy_protocols`，不受
`PROTOCOLS_ENABLE_*` 选项门控。

## 目录结构

```
core/
├── adapter/                     # 协议扩展框架
│   ├── include/protocol_extension_framework.h
│   └── src/
│       ├── protocol_extension_framework.c
│       ├── protocol_extension_framework_registry.c
│       ├── protocol_extension_framework_route.c
│       ├── protocol_extension_framework_config.c
│       └── protocol_extension_framework_middleware.c
├── registry/                    # 协议注册中心
│   ├── include/protocol_registry.h
│   └── src/protocol_registry.c
├── router/                      # 协议路由引擎
│   ├── include/protocol_router.h
│   └── src/protocol_router.c
└── transformers/                # 消息转换器
    ├── include/protocol_transformers.h
    └── src/protocol_transformers.c
```

## 组件一览

| 组件 | 头文件 | 职责 | 详细说明 |
|------|--------|------|----------|
| Router | `protocol_router.h` | 基于规则的跨协议路由与即时转换，支持单条与批量 | [core/router](router/README.md) |
| Transformers | `protocol_transformers.h` | JSON-RPC ⇄ MCP/A2A/OpenAI/OpenJiuwen 双向转换 | [core/transformers](transformers/README.md) |
| Extension Framework | `protocol_extension_framework.h` | 第三方适配器注册、生命周期、中间件链、版本协商、热加载 | [core/adapter](adapter/README.md) |
| Registry | `protocol_registry.h` | 协议条目注册 / 发现 / 分类 / 依赖 / 统计 / JSON 导出 | [core/registry](registry/README.md) |

## 组件协作

```
                 ┌──────────────────┐
                 │     Registry     │  协议注册 / 发现 / 能力查询
                 └────────┬─────────┘
                          │
                 ┌────────▼─────────┐
                 │      Router      │  规则匹配 / 消息路由
                 └────────┬─────────┘
                          │ 套用
        ┌─────────────────┼─────────────────┐
┌───────▼────────┐ ┌──────▼───────┐ ┌───────▼────────┐
│  Transformers  │ │   Extension  │ │    消费方      │
│  (格式互转)    │ │   Framework  │ │   (gateway     │
└────────────────┘ │  (插件管理)  │ │    等)         │
                   └──────────────┘ └────────────────┘
```

## 依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| `unified_protocol.h` | `protocols/include/` | 统一消息模型与协议类型 |
| `airy_protocol_interface.h` | `protocols/include/` | 适配器虚表与分层接口 |
| commons | [commons 仓库](https://atomgit.com/openairymax/commons) | 内存/字符串/错误码等平台封装 |

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
