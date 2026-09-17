# router — 协议路由引擎

**位置：** `protocols/core/router/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

协议路由引擎（Protocol Router）基于可配置的规则表做跨协议消息
路由：按源协议、目标协议与端点模式匹配规则，命中即套用该规则
绑定的转换器输出目标消息；无命中时按创建时指定的默认协议直通、
不做转换。支持单条与批量路由，并可替换整体决策策略。

> 同名头文件在 `protocols/include/` 亦有一份；本页描述的是路由引擎
> `core/router/include/protocol_router.h` 的 API。

## 目录结构

```
router/
├── include/
│   └── protocol_router.h          # 路由引擎头文件
├── src/
│   └── protocol_router.c          # 路由引擎实现
└── README.md                      # 本文件
```

## 数据类型

| 类型 | 说明 |
|------|------|
| `protocol_rule_t` | 路由规则：源 / 目标协议、源 / 目标端点模式（支持通配符）、优先级、转换器上下文 |
| `message_transformer_t` | 消息转换函数类型：接收源消息与上下文，输出目标消息 |
| `route_decision_func_t` | 自定义决策函数类型：按消息与规则表返回匹配规则索引（或 -1） |
| `protocol_router_handle_t` | 路由引擎实例句柄（不透明） |

## 核心 API

| 函数 | 说明 |
|------|------|
| `protocol_router_create(default_protocol)` | 创建实例，指定无匹配时的默认协议 |
| `protocol_router_destroy()` | 销毁实例 |
| `protocol_router_add_rule()` | 添加规则并绑定转换器（传 NULL 用默认转换） |
| `protocol_router_remove_rule()` | 按源端点模式删除规则 |
| `protocol_router_route()` | 路由单条消息：匹配 → 转换 → 输出 |
| `protocol_router_route_batch()` | 批量路由，返回成功条数 |
| `protocol_router_set_decision_func()` | 替换自定义决策函数 |
| `protocol_router_get_stats()` | 获取路由统计（JSON 字符串，由调用方释放） |

## 便捷转换器

路由头文件同时声明 5 个可直接绑定给规则的转换器：

| 转换器 | 说明 |
|--------|------|
| `protocol_transformer_jsonrpc_to_mcp()` | JSON-RPC → MCP |
| `protocol_transformer_mcp_to_jsonrpc()` | MCP → JSON-RPC |
| `protocol_transformer_openai_to_jsonrpc()` | OpenAI → JSON-RPC |
| `protocol_transformer_a2a_to_jsonrpc()` | A2A → JSON-RPC |
| `protocol_transformer_default()` | 默认转换器（直接复制，无格式转换） |

更细粒度的字段级映射（如 `skill.execute` → `tools/call`）由
[core/transformers](../transformers/README.md) 提供。

## 路由流程

```
输入消息 → 规则匹配 → [命中?]
                       ├─ 是 → 执行绑定转换器 → 输出目标消息
                       └─ 否 → 按默认协议直通（不转换）
```

## 用法

```c
#include "protocol_router.h"

/* 默认协议为 HTTP/JSON-RPC */
protocol_router_handle_t router = protocol_router_create(PROTOCOL_HTTP);

protocol_rule_t rule = {
    .source_protocol = PROTO_JSONRPC,
    .target_protocol = PROTO_MCP,
    .source_endpoint = "/mcp/*",
    .target_endpoint = "/mcp/v1",
    .priority = 10,
    .transformer_context = NULL,
};
protocol_router_add_rule(router, &rule, protocol_transformer_jsonrpc_to_mcp);

unified_message_t transformed;
protocol_router_route(router, &input_msg, &transformed);

unified_message_t outputs[10];
int routed = protocol_router_route_batch(router, messages, 10, outputs);

char *stats_json = NULL;
protocol_router_get_stats(router, &stats_json);
/* 使用后可按源端点模式移除规则 */
protocol_router_remove_rule(router, "/mcp/*");

free(stats_json);
protocol_router_destroy(router);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
