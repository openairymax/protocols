# OpenJiuwen 二进制协议适配器

**位置：** `protocols/integrations/openjiuwen/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 OpenJiuwen 平台自定义二进制协议适配器
（`OPENJIUWEN_PROTOCOL_VERSION` 为 `"1.0.0"`）：负责统一协议消息
（`unified_message_t`）与原生二进制帧（消息头 + 载荷 + CRC32 校验尾）
之间的双向编解码，并维护带指数退避自动重连（1 s 至 60 s）、30 秒心跳
与连续错误计数的连接状态机。适配器实例以 `protocol_adapter_t` 基类
扩展结构呈现，可直接注册进协议注册表与路由器。

## 目录结构

```
openjiuwen/
├── include/
│   └── openjiuwen_adapter.h       # 公开头文件
└── src/
    ├── openjiuwen_adapter.c       # 上下文、vtable、配置
    ├── openjiuwen_adapter_msg.c   # 消息编解码与 CRC32
    ├── openjiuwen_adapter_io.c    # 收发队列与帧读写
    ├── openjiuwen_adapter_net.c   # 连接、心跳与重连
    └── openjiuwen_adapter_internal.h # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `OPENJIUWEN_PROTOCOL_VERSION` | `"1.0.0"` | 协议版本 |
| `OPENJIUWEN_MAX_MESSAGE_SIZE` | 65536（64 KB） | 单条消息上限 |
| `OPENJIUWEN_DEFAULT_ENDPOINT` | `"/openjiuwen/v1"` | 默认端点 |
| `OPENJIUWEN_TIMEOUT_MS` | 30000 | 默认超时（毫秒） |
| `OPENJIUWEN_HEARTBEAT_INTERVAL_SEC` | 30 | 心跳间隔（秒） |
| `OPENJIUWEN_MAX_CONSECUTIVE_ERRORS` | 5 | 触发重连的连续错误数 |
| `OPENJIUWEN_RECONNECT_BASE_DELAY_MS` | 1000 | 重连基础退避（毫秒） |
| `OPENJIUWEN_RECONNECT_MAX_DELAY_MS` | 60000 | 重连最大退避（毫秒） |
| `OPENJIUWEN_SEND_QUEUE_SIZE` | 64 | 发送队列深度 |

## 枚举

### 消息类型（`openjiuwen_message_type_t`，5 种）

| 枚举 | 值 | 说明 |
|------|-----|------|
| `OPENJIUWEN_MSG_TYPE_REQUEST` | 0x0001 | 请求 |
| `OPENJIUWEN_MSG_TYPE_RESPONSE` | 0x0002 | 响应 |
| `OPENJIUWEN_MSG_TYPE_NOTIFICATION` | 0x0003 | 通知 |
| `OPENJIUWEN_MSG_TYPE_HEARTBEAT` | 0x0004 | 心跳 |
| `OPENJIUWEN_MSG_TYPE_ERROR` | 0x0005 | 错误 |

### 连接状态（`openjiuwen_conn_state_t`，5 态）

`OPENJIUWEN_CONN_DISCONNECTED` → `_CONNECTING` → `_CONNECTED`；
错误恢复路径 `_RECONNECTING`，失败进入 `_ERROR`。

## 主要类型

### `openjiuwen_header_t`（消息头字段）

| 字段 | 类型 | 说明 |
|------|------|------|
| `message_id` | `uint32_t` | 消息 ID |
| `timestamp` | `uint32_t` | 时间戳 |
| `message_type` | `uint16_t` | 消息类型（见上表） |
| `flags` | `uint16_t` | 标志位 |
| `payload_length` | `uint32_t` | 载荷长度 |
| `source_agent[64]` / `target_agent[64]` | `char` 数组 | 源/目标 Agent ID |

### 其他类型

| 类型 | 说明 |
|------|------|
| `openjiuwen_config_t` | 配置：endpoint[256]、api_key[128]、超时、压缩/加密开关、最大重试 |
| `openjiuwen_adapter_t` | 适配器实例：内嵌 `protocol_adapter_t base` + 连接状态、错误/重连计数、心跳与活动时间戳 |

## 核心 API

| 函数 | 说明 |
|------|------|
| `openjiuwen_adapter_create(config)` | 创建适配器，返回 `const protocol_adapter_t *` |
| `openjiuwen_get_default_config(&config)` | 填充默认配置 |
| `openjiuwen_verify_connection(adapter)` | 连接可用性校验 |
| `openjiuwen_get_capabilities(adapter, buf, size)` | 能力描述（字符串输出） |
| `openjiuwen_unified_to_native(msg, out_buffer, ...)` | 统一消息 → 二进制帧（含 CRC32 尾） |
| `openjiuwen_native_to_unified(in_buffer, size, msg)` | 二进制帧 → 统一消息（先验 CRC32） |

## 用法示例

```c
#include "openjiuwen_adapter.h"

openjiuwen_config_t cfg;
openjiuwen_get_default_config(&cfg);
cfg.enable_compression = true;

const protocol_adapter_t *adapter = openjiuwen_adapter_create(&cfg);

unified_message_t msg = {0};
/* ... 填充 msg ... */
uint8_t frame[OPENJIUWEN_MAX_MESSAGE_SIZE];
size_t frame_len = sizeof(frame);
if (openjiuwen_unified_to_native(&msg, frame, &frame_len) == 0) {
    /* frame 可直接写入传输通道 */
}

unified_message_t back = {0};
openjiuwen_native_to_unified(frame, frame_len, &back);
```

## 构建

本目录源码无条件编译进 `airy_protocols` 静态库；
`PROTOCOLS_ENABLE_OPENJIUWEN` 为功能开关（feature flag），默认启用。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的测试位于仓库 `tests/` 目录（`test_openjiuwen_adapter.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
