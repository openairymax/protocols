# adapter — 协议扩展框架

**位置：** `protocols/core/adapter/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

协议扩展框架（Protocol Extension Framework）是 Core 层的插件式适配器
管理组件，供第三方开发者创建、注册并运行自定义协议适配器。设计要点：

1. **插件式协议注册** — 动态加载 / 卸载协议适配器，单框架最多 64 个
2. **可组合中间件管线** — 按优先级串联的消息处理链，最多 32 个中间件
3. **能力声明与发现** — 协议能力注册与按能力查找，最多 128 种能力
4. **版本协商** — 客户端-服务端协议版本协商回调
5. **运行时热加载** — 从 JSON 配置批量加载扩展

## 目录结构

```
adapter/
├── include/
│   └── protocol_extension_framework.h    # 扩展框架头文件
├── src/
│   ├── protocol_extension_framework.c        # 框架生命周期与核心
│   ├── protocol_extension_framework_registry.c  # 适配器注册表
│   ├── protocol_extension_framework_route.c     # 自动路由
│   ├── protocol_extension_framework_config.c    # JSON 配置加载
│   └── protocol_extension_framework_middleware.c # 中间件链
└── README.md                               # 本文件
```

## 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `PROTO_EXT_MAX_ADAPTERS` | 64 | 最大适配器数量 |
| `PROTO_EXT_MAX_MIDDLEWARE` | 32 | 最大中间件数量 |
| `PROTO_EXT_MAX_CAPABILITIES` | 128 | 最大能力条目数 |
| `PROTO_EXT_NAME_LEN` | 64 | 名称最大长度 |
| `PROTO_EXT_VERSION_LEN` | 32 | 版本字符串最大长度 |

## 数据类型

| 类型 | 说明 |
|------|------|
| `proto_ext_descriptor_t` | 扩展描述符：`name`、`version`、`description[256]`、`author[128]`、`protocol_type`、`capabilities`、`priority`、`hot_loadable`、`requires_auth` |
| `proto_ext_callbacks_t` | 回调集合：生命周期 `on_load` / `on_init` / `on_start` / `on_stop` / `on_unload`，消息面 `encode_message` / `decode_message` / `handle_request`，扩展面 `negotiate_version` / `get_capabilities` / `on_error` |
| `proto_middleware_t` | 中间件：名称、处理函数、优先级、用户数据、启用标志 |
| `proto_ext_state_t` | 适配器状态：`UNLOADED` / `LOADED` / `INITIALIZED` / `RUNNING` / `ERROR` / `DISABLED` |
| `proto_ext_priority_t` | 优先级：`LOWEST`(0) / `LOW`(25) / `NORMAL`(50) / `HIGH`(75) / `HIGHEST`(100) |
| `proto_ext_stats_t` | 运行统计：名称、状态、错误数、最后活动时间、已处理消息数 |

## 核心 API

| 函数 | 说明 |
|------|------|
| `proto_ext_framework_create()` / `proto_ext_framework_destroy()` | 创建 / 销毁框架实例 |
| `proto_ext_get_global_instance()` | 获取全局框架实例 |
| `proto_ext_register()` / `proto_ext_unregister()` | 注册 / 注销扩展（描述符 + 回调集） |
| `proto_ext_load()` / `proto_ext_unload()` | 加载 / 卸载扩展（加载时传入配置 JSON） |
| `proto_ext_start()` / `proto_ext_stop()` | 启动 / 停止扩展 |
| `proto_ext_send_message()` | 经指定适配器发送统一消息 |
| `proto_ext_handle_request()` | 经指定适配器处理请求 |
| `proto_ext_auto_route()` | 自动路由消息到匹配的适配器 |
| `proto_ext_negotiate()` | 协议版本协商 |
| `proto_ext_add_middleware()` / `proto_ext_remove_middleware()` | 添加 / 移除中间件 |
| `proto_ext_enable_middleware()` / `proto_ext_disable_middleware()` | 启用 / 禁用中间件 |
| `proto_ext_process_middleware_chain()` | 按优先级执行中间件链 |
| `proto_ext_list_adapters()` / `proto_ext_list_capabilities()` | 列出已注册适配器 / 能力 |
| `proto_ext_find_by_capability()` | 按能力标志查找适配器 |
| `proto_ext_get_state()` / `proto_ext_get_adapter_stats()` | 查询适配器状态 / 统计 |
| `proto_ext_get_framework_adapter()` | 取扩展注册为的统一适配器（`const protocol_adapter_t *`） |
| `proto_ext_load_from_config()` | 从 JSON 配置批量加载扩展 |

## 适配器生命周期

```
UNLOADED → LOADED → INITIALIZED → RUNNING
              ↑         ↓            ↓
              └── DISABLED ←── ERROR ←┘
```

## 用法

```c
#include "protocol_extension_framework.h"

proto_ext_framework_t *fw = proto_ext_framework_create();

proto_ext_descriptor_t desc = {
    .name = "my-protocol",
    .version = "1.0.0",
    .description = "My custom protocol adapter",
    .author = "example",
    .protocol_type = PROTOCOL_CUSTOM,
    .capabilities = PROTO_CAP_STREAMING | PROTO_CAP_FUNCTION_CALLING,
    .priority = PROTO_EXT_PRIORITY_NORMAL,
    .hot_loadable = true,
    .requires_auth = false,
};

proto_ext_register(fw, &desc, &callbacks);
proto_ext_load(fw, "my-protocol", "{\"endpoint\":\"localhost:8080\"}");
proto_ext_start(fw, "my-protocol");

proto_ext_add_middleware(fw, "logging", my_logging_middleware,
                         PROTO_EXT_PRIORITY_LOW, NULL);

proto_ext_send_message(fw, "my-protocol", &message);

proto_ext_stop(fw, "my-protocol");
proto_ext_unload(fw, "my-protocol");
proto_ext_unregister(fw, "my-protocol");
proto_ext_framework_destroy(fw);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
