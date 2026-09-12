# registry — 协议注册中心

**位置：** `protocols/core/registry/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

协议注册中心（Protocol Registry）为所有协议适配器（内置与自定义）
提供统一的登记与治理：注册 / 注销、按名称 / 类型 / 能力发现、
五级分类、状态机管理、依赖追踪、运行时统计与 JSON 导出。
注册中心模块自身以 `PROTO_REGISTRY_VERSION`（当前 `"2.1.0"`）标注版本。

## 目录结构

```
registry/
├── include/
│   └── protocol_registry.h        # 注册中心头文件
├── src/
│   └── protocol_registry.c        # 注册中心实现
└── README.md                      # 本文件
```

## 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `PROTO_REGISTRY_MAX_ADAPTERS` | 32 | 最大条目数（构建时以同名编译定义注入） |
| `PROTO_REGISTRY_MAX_DEPS` | 16 | 单条目最大依赖声明数 |
| `PROTO_REGISTRY_NAME_MAX_LEN` | 64 | 名称最大长度 |
| `PROTO_REGISTRY_DESC_MAX_LEN` | 256 | 描述最大长度 |

## 数据类型

| 类型 | 说明 |
|------|------|
| `proto_registry_entry_t` | 注册条目：名称、版本、描述、分类、协议类型、能力标志、状态、适配器指针、依赖列表、注册/激活时间、心跳、请求数、错误数、平均延迟、内置标志 |
| `proto_category_t` | 协议分类：`CORE` / `STANDARD` / `INTEGRATION` / `FRAMEWORK` / `CUSTOM` |
| `proto_state_t` | 协议状态：`UNREGISTERED` / `REGISTERED` / `INITIALIZING` / `READY` / `ACTIVE` / `DEGRADED` / `ERROR` / `SHUTDOWN` |
| `proto_dependency_t` | 依赖声明：被依赖名称与其实时状态 |
| `proto_registry_stats_t` | 全局统计：总条目 / 活跃 / 内置 / 自定义数、总请求、总错误、运行时长、平均延迟 |
| `proto_registry_event_fn` | 状态变更事件回调类型 |

## 核心 API

| 函数 | 说明 |
|------|------|
| `proto_registry_create()` / `proto_registry_destroy()` | 创建 / 销毁注册中心实例 |
| `proto_registry_version()` | 返回注册中心模块版本字符串 |
| `proto_registry_register()` / `proto_registry_unregister()` | 注册 / 注销协议条目 |
| `proto_registry_find()` / `find_by_type()` / `find_by_capability()` | 按名称 / 类型 / 能力标志查找 |
| `proto_registry_list_all()` / `list_by_category()` / `list_active()` | 列出全部 / 按分类 / 全部活跃条目 |
| `proto_registry_set_state()` | 设置条目状态 |
| `proto_registry_add_dependency()` | 添加依赖声明 |
| `proto_reg_check_deps()` | 检查条目依赖是否全部满足 |
| `proto_registry_activate()` / `proto_registry_deactivate()` | 激活 / 停用（激活前校验依赖） |
| `proto_registry_heartbeat()` | 心跳，更新最后活跃时间 |
| `proto_registry_record_request()` | 记录请求结果与延迟 |
| `proto_registry_get_statistics()` | 获取全局统计 |
| `proto_registry_export_json()` | 导出注册信息为 JSON |
| `proto_registry_set_event_callback()` | 设置状态变更事件回调 |
| `proto_registry_initialize_builtins()` | 注册并激活 9 个内置协议条目 |
| `proto_category_to_string()` / `proto_state_to_string()` | 枚举值转字符串 |

### 内置协议条目

`proto_registry_initialize_builtins()` 登记以下 9 个条目（JSON-RPC 2.0、
MCP 1.0、A2A 0.3、OpenAI、OpenJiuwen、OpenClaw、Claude、AGNTCY、
ChinaEco 各 1.0），标注其分类与能力标志并逐一激活，返回登记数量。

## 协议状态机

```
UNREGISTERED → REGISTERED → INITIALIZING → READY → ACTIVE
                  ↑              ↓           ↓  ↕    ↓
                  └──── SHUTDOWN ← ERROR ← DEGRADED ←┘
```

## 用法

```c
#include "protocol_registry.h"

protocol_registry_t *registry = proto_registry_create();

/* 登记并激活 9 个内置协议 */
int count = proto_registry_initialize_builtins(registry);

/* 注册自定义协议 */
proto_registry_register(registry, "my-proto", "1.0.0",
                        "My custom protocol", PROTO_CAT_CUSTOM,
                        PROTOCOL_CUSTOM, PROTO_CAP_STREAMING,
                        &my_adapter, NULL);

/* 查找与激活 */
proto_registry_entry_t *entry = proto_registry_find(registry, "my-proto");
proto_registry_add_dependency(registry, "my-proto", "JSON-RPC");
proto_registry_activate(registry, "my-proto");

/* 记录请求并获取统计 */
proto_registry_record_request(registry, "my-proto", true, 12.5);
proto_registry_stats_t stats;
proto_registry_get_statistics(registry, &stats);

proto_registry_destroy(registry);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
