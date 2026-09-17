# OpenClaw 平台适配器

**位置：** `protocols/integrations/openclaw/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 OpenClaw 平台集成适配器（`OPENCLAW_ADAPTER_VERSION` 为
`"1.0.0"`）：通过带 `OCLW` 魔数前缀的消息帧与平台侧 Unix domain socket
通信，提供 Agent 注册/发现/注销、跨 Agent 工具共享、任务委派与查询/
取消、多会话管理、心跳与集群状态统计，并支持六级 Agent 状态机、五级
安全等级与六种模态（文本/图像/音频/视频/文件/代码）的能力映射。

## 目录结构

```
openclaw/
├── include/
│   └── openclaw_adapter.h        # 公开头文件
└── src/
    ├── openclaw_adapter.c        # 上下文、配置、vtable
    ├── openclaw_adapter_cb.c     # 回调与消息分发
    ├── openclaw_adapter_socket.c # Unix socket 传输（POSIX）
    ├── openclaw_adapter_registry.c # Agent/工具注册表
    ├── openclaw_adapter_session.c  # 会话管理
    ├── openclaw_adapter_monitor.c  # 心跳与集群状态
    ├── openclaw_adapter_task.c     # 任务委派
    └── openclaw_adapter_internal.h # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `OPENCLAW_ADAPTER_VERSION` | `"1.0.0"` | 适配器版本 |
| `OPENCLAW_PLATFORM_VERSION` | 版本字符串 | 握手时报出的模拟平台版本 |
| `OPENCLAW_MAGIC_PREFIX` | `"OCLW"` | 消息帧魔数前缀 |
| `OPENCLAW_MAX_AGENTS` | 64 | Agent 注册容量 |
| `OPENCLAW_MAX_TOOLS` | 256 | 工具注册容量 |
| `OPENCLAW_MAX_SESSIONS` | 128 | 并行会话上限 |
| `OPENCLAW_MAX_CONTEXT_KB` | 1024 | 会话上下文上限（KB） |
| `OPENCLAW_HEARTBEAT_INTERVAL_SEC` | 30 | 心跳间隔（秒） |
| `OPENCLAW_DEFAULT_TIMEOUT_MS` | 30000 | 默认超时（毫秒） |

## 枚举

### 部署模式（`openclaw_mode_t`，4 种）

`OPENCLAW_MODE_STANDALONE`、`OPENCLAW_MODE_CLUSTERED`、
`OPENCLAW_MODE_HYBRID`、`OPENCLAW_MODE_EMBEDDED`

### 安全等级（`openclaw_security_level_t`，5 级）

`OPENCLAW_SECURITY_LEVEL_PUBLIC`、`_INTERNAL`、`_CONFIDENTIAL`、
`_SECRET`、`_TOP_SECRET`

### 模态（`openclaw_modality_t`，位标志）

`OPENCLAW_MODALITY_TEXT`（0x01）、`_IMAGE`（0x02）、`_AUDIO`（0x04）、
`_VIDEO`（0x08）、`_FILE`（0x10）、`_CODE`（0x20）、
`_ALL`（0x3F）

### Agent 状态（`openclaw_agent_state_t`，6 态）

`OPENCLAW_AGENT_STATE_IDLE`、`_THINKING`、`_EXECUTING`、`_WAITING`、
`_ERROR`、`_TERMINATED`

## 主要类型

| 类型 | 说明 |
|------|------|
| `openclaw_config_t` | 连接与运行配置（socket 路径、模式、安全等级等） |
| `openclaw_adapter_context_t` | 适配器上下文（不透明句柄） |
| `openclaw_agent_card_t` / `openclaw_tool_info_t` | Agent 卡片与工具条目 |
| `openclaw_session_t` / `openclaw_message_t` / `openclaw_task_t` | 会话、消息与任务实体 |
| `openclaw_cluster_status_t` | 集群统计（节点/Agent/会话/任务） |

## 核心 API

### 生命周期与连接

| 函数 | 说明 |
|------|------|
| `openclaw_config_default()` | 返回默认配置 |
| `openclaw_adapter_create(config)` / `openclaw_adapter_destroy(ctx)` | 上下文生命周期 |
| `openclaw_adapter_is_initialized(ctx)` / `openclaw_adapter_version()` / `openclaw_adapter_platform_version()` | 状态与版本查询 |
| `openclaw_connect(ctx)` / `openclaw_disconnect(ctx)` / `openclaw_is_connected(ctx)` | socket 连接管理 |

### Agent、工具、会话与任务

| 函数 | 说明 |
|------|------|
| `openclaw_register_agent` / `openclaw_discover_agents` / `openclaw_unregister_agent` | Agent 注册、按能力过滤发现、注销 |
| `openclaw_register_tool` / `openclaw_list_tools` | 工具注册与列举 |
| `openclaw_create_session` / `openclaw_close_session` | 会话创建与关闭 |
| `openclaw_send_message` | 消息发送 |
| `openclaw_delegate_task` / `openclaw_query_task` / `openclaw_cancel_task` | 任务委派、查询、取消 |
| `openclaw_get_cluster_status` | 集群状态读取 |

### 回调、心跳与集成

| 函数 | 说明 |
|------|------|
| `openclaw_set_message_handler` / `openclaw_set_task_handler` / `openclaw_set_event_callback` / `openclaw_set_status_callback` | 四类回调注册 |
| `openclaw_send_heartbeat(ctx)` | 发送心跳帧 |
| `openclaw_get_statistics(ctx, stats_json, buffer_size)` | 统计信息（JSON 输出） |
| `openclaw_get_protocol_adapter()` | 返回统一 vtable（`proto_adapter_t`） |
| `openclaw_agent_card_destroy` / `openclaw_tool_info_destroy` / `openclaw_session_destroy` / `openclaw_message_destroy` / `openclaw_task_destroy` / `openclaw_cluster_status_destroy` | 实体释放 |

## 用法示例

```c
#include "openclaw_adapter.h"

openclaw_config_t cfg = openclaw_config_default();
cfg.mode = OPENCLAW_MODE_STANDALONE;
openclaw_adapter_context_t *ctx = openclaw_adapter_create(&cfg);

if (openclaw_connect(ctx) == 0) {
    openclaw_agent_card_t card = {0};
    /* ... 填充 card ... */
    openclaw_register_agent(ctx, &card);
    openclaw_send_heartbeat(ctx);
}

const proto_adapter_t *adapter = openclaw_get_protocol_adapter();
/* 注册进统一协议栈 */

openclaw_disconnect(ctx);
openclaw_adapter_destroy(ctx);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_OPENCLAW` 门控，**默认 `OFF`**，
需显式开启；因依赖 Unix domain socket，Windows 上强制 `OFF`。
构建方式见[主文档「构建」一节](../../README_zh.md#构建)。
对应的测试位于仓库 `tests/` 目录（`test_openclaw_adapter.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
