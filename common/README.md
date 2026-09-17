# common — 协议门面与协议栈管理

**位置：** `protocols/common/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`common/` 是整个协议层的门面（facade）：一个统一入口头文件
`protocols.h`，加上框架初始化、协议栈管理器与统一协议栈的两个实现
文件。调用方包含这一个头，即可完成「初始化框架 → 创建管理器 →
创建协议栈 → 注册适配器 → 收发统一消息」的完整生命周期。

统一消息模型本身（`unified_message_t`、`airy_protocol_type_t`、
`protocol_adapter_t` 等类型）定义在上层 [`include/unified_protocol.h`](../include/unified_protocol.h)，
本目录提供的是围绕它的管理器与协议栈实现。

## 目录结构

```
common/
├── include/
│   └── protocols.h            # 框架主头文件（统一入口）
├── src/
│   ├── protocols_impl.c       # 框架初始化、管理器、配置与错误处理
│   └── unified_protocol.c     # 协议栈核心实现
└── README.md                  # 本文件
```

## API 分组

### 框架与管理器（`protocols_impl.c`）

| 函数 | 说明 |
|------|------|
| `protocols_framework_init()` / `protocols_framework_cleanup()` | 框架全局初始化 / 清理 |
| `protocols_framework_version()` | 返回框架版本号 |
| `protocol_manager_create()` / `protocol_manager_destroy()` | 创建 / 销毁管理器（每个管理器最多承载 32 个协议栈） |
| `protocol_manager_create_stack()` / `protocol_manager_destroy_stack()` | 在管理器中创建 / 销毁协议栈 |
| `protocol_manager_get_stacks()` | 枚举管理器当前持有的协议栈 |
| `protocol_stack_config_default(name)` | 生成默认栈配置：`PROTOCOL_HTTP`（JSON-RPC 2.0 别名）、消息上限 64KB、超时 30s、压缩/加密关闭 |
| `protocol_stack_config_destroy()` | 释放配置持有的资源 |
| `protocol_error_to_string()` / `protocol_get_last_error()` | 错误码描述与最近错误查询 |
| `protocol_adapter_http()` | 内置 HTTP/JSON-RPC 适配器工厂——门面中唯一的通用适配器工厂，返回 `const protocol_adapter_t *` |

### 协议栈（`unified_protocol.c`）

| 函数 | 说明 |
|------|------|
| `protocol_stack_create()` / `protocol_stack_destroy()` | 创建 / 销毁协议栈实例 |
| `protocol_stack_register_adapter()` | 注册适配器（同类型可替换），按值接收 `protocol_adapter_t` |
| `protocol_stack_send()` / `protocol_stack_receive()` | 按消息协议匹配适配器编码发送 / 遍历适配器接收解码 |
| `protocol_stack_set_callback()` | 设置消息到达回调 |
| `protocol_stack_get_stats()` | 获取发送 / 接收 / 字节统计 |
| `unified_message_create()` / `unified_message_destroy()` | 创建统一消息（自动分配消息 ID 与时间戳）/ 销毁（payload 由调用方管理） |
| `protocol_type_to_string()` / `protocol_type_from_string()` | 协议类型与字符串互转 |

## 错误码

`protocol_error_t` 共 11 个取值：

`PROTOCOL_SUCCESS`、`PROTOCOL_ERROR_INVALID_ARG`、`PROTOCOL_ERROR_MEMORY`、
`PROTOCOL_ERROR_NOT_INITIALIZED`、`PROTOCOL_ERROR_NOT_CONNECTED`、
`PROTOCOL_ERROR_TIMEOUT`、`PROTOCOL_ERROR_ENCODE`、`PROTOCOL_ERROR_DECODE`、
`PROTOCOL_ERROR_NETWORK`、`PROTOCOL_ERROR_PROTOCOL`、`PROTOCOL_ERROR_INTERNAL`。

## 用法

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

unified_message_t recv_msg;
if (protocol_stack_receive(stack, &recv_msg, 5000) == 0) {
    /* 处理接收到的消息 */
}

protocol_manager_destroy_stack(mgr, stack);
protocol_manager_destroy(mgr);
protocols_framework_cleanup();
```

各协议的专用适配器不在门面中创建，而是经由各自目录的构造函数
（如 `openjiuwen_adapter_create()`）或通过扩展框架与注册表注册，
见主文档[构成](../README_zh.md#构成)一节。

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库，无独立 CMake 选项；
构建方式见[主文档「构建」一节](../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
