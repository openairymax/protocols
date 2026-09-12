# autogen — AutoGen 多代理框架适配器

**位置：** `protocols/frameworks/autogen/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 Microsoft AutoGen 多代理对话框架适配器：将 AutoGen 的
ConversableAgent、GroupChat、CodeExecutor 等核心对象模型映射到统一协议层，
支持多代理角色定义与消息收发、群聊编排（轮询/发言者选择等模式）、代码执行
与人机协作回调挂接、对话历史获取，以及工具注册与调用。适配器同时以
`proto_adapter_t` 虚表形式接入协议注册表。

## 目录结构

```
autogen/
├── README.md
├── include/
│   └── autogen_adapter.h          # 公开头文件
└── src/
    ├── autogen_adapter.c          # 适配器生命周期与配置
    ├── autogen_adapter_agent.c    # Agent 创建/销毁/列举
    ├── autogen_adapter_msg.c      # 消息与群聊编排
    ├── autogen_adapter_proto.c    # proto_adapter_t 虚表接入
    └── autogen_adapter_internal.h # 内部共享声明
```

配套测试：`tests/test_autogen_adapter.c`。

## 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `AUTOGEN_ADAPTER_VERSION` | `"1.0.0"` | 适配器版本字符串 |
| `AUTOGEN_MAX_AGENTS` | 32 | 最大 Agent 数量 |
| `AUTOGEN_MAX_GROUP_CHATS` | 16 | 最大群聊数量 |
| `AUTOGEN_MAX_MESSAGES` | 512 | 最大消息数量 |
| `AUTOGEN_MAX_TOOLS` | 64 | 最大工具数量 |
| `AUTOGEN_MAX_RESPONSE_LEN` | 8192 | 响应缓冲上限（字节） |
| `AUTOGEN_DEFAULT_TIMEOUT_MS` | 120000 | 默认超时（毫秒） |

## 枚举

Agent 角色 `autogen_agent_role_t`：
`AGENT_ROLE_USER_PROXY`、`AGENT_ROLE_ASSISTANT`、`AGENT_ROLE_CODER`、
`AGENT_ROLE_PLANNER`、`AGENT_ROLE_RESEARCHER`、`AGENT_ROLE_REVIEWER`、
`AGENT_ROLE_CUSTOM`。

群聊模式 `autogen_chat_mode_t`：
`GROUP_CHAT_ROUND_ROBIN`、`GROUP_CHAT_SPEAKER_SELECTION`、`GROUP_CHAT_RAG`、
`GROUP_CHAT_SEQUENTIAL`、`GROUP_CHAT_PARALLEL`、`GROUP_CHAT_CUSTOM`。

消息类型 `autogen_message_type_t`：
`MSG_TYPE_TEXT`、`MSG_TYPE_TOOL_CALL`、`MSG_TYPE_TOOL_RESULT`、
`MSG_TYPE_CODE_BLOCK`、`MSG_TYPE_TERMINATION`、`MSG_TYPE_HANDOFF`、
`MSG_TYPE_SYSTEM`。

人机协作模式 `autogen_human_mode_t`（4 种）：
`HUMAN_MODE_NEVER`、`HUMAN_MODE_TERMINATE`、`HUMAN_MODE_ALWAYS`、
`HUMAN_MODE_CODE_EXECUTION`。

## 主要类型

| 类型 | 说明 |
|------|------|
| `autogen_config_t` | 适配器配置（base URL、API Key、超时、代码执行/人机协作/流式开关、工作目录等） |
| `autogen_adapter_context_t` | 适配器上下文（不透明句柄，经 API 操作） |
| `autogen_agent_def_t` / `autogen_agent_instance_t` | Agent 定义（角色、系统消息、终止条件、人机模式）与运行实例 |
| `autogen_group_chat_def_t` | 群聊定义（模式、参与者、发言者选择提示词、轮次上限） |
| `autogen_message_t` | 消息（发送者/接收者、类型、内容、工具调用、元数据） |
| `autogen_conversation_t` | 对话记录（消息列表、摘要、终止原因） |
| `autogen_group_chat_result_t` | 群聊执行结果（对话、耗时、轮次、最终摘要） |
| `autogen_tool_executor_fn` 等回调 | 工具执行、代码执行、人机交互、消息钩子、LLM 调用共 5 类回调类型 |

## 核心 API

### 生命周期

| 函数 | 说明 |
|------|------|
| `autogen_config_default()` | 返回默认配置（按值返回） |
| `autogen_adapter_create(config)` | 创建适配器上下文 |
| `autogen_adapter_destroy(ctx)` | 销毁适配器上下文 |
| `autogen_adapter_is_initialized(ctx)` | 查询初始化状态 |
| `autogen_adapter_version()` | 返回版本字符串 |

### Agent 与群聊

| 函数 | 说明 |
|------|------|
| `autogen_create_agent(ctx, definition, out_agent_id)` | 创建 Agent，输出 ID 写入调用方缓冲 |
| `autogen_destroy_agent(ctx, agent_id)` | 销毁 Agent |
| `autogen_list_agents(ctx, agents, count)` | 列举 Agent 实例 |
| `autogen_create_group_chat(ctx, definition, out_group_id)` | 创建群聊，输出 ID 写入调用方缓冲 |
| `autogen_initiate_chat(ctx, group_id, sender_id, message, result)` | 在群聊中发起对话 |
| `autogen_send_message(ctx, from_agent_id, to_agent_id, content, type, reply)` | Agent 间发送消息并取回复 |
| `autogen_get_conversation(ctx, group_id, conv)` | 获取对话记录 |

### 工具与回调

| 函数 | 说明 |
|------|------|
| `autogen_register_tool(ctx, name, description, schema_json, executor, user_data)` | 注册工具及执行回调 |
| `autogen_set_code_executor(ctx, executor, user_data)` | 设置代码执行器回调 |
| `autogen_set_human_callback(ctx, callback, user_data)` | 设置人机协作回调 |
| `autogen_set_message_hook(ctx, hook, user_data)` | 设置消息钩子回调 |
| `autogen_set_llm_callback(ctx, callback, user_data)` | 设置 LLM 调用回调 |
| `autogen_get_statistics(ctx, stats_json, buffer_size)` | 导出 JSON 格式统计 |

### 协议接入与实体释放

| 函数 | 说明 |
|------|------|
| `autogen_get_protocol_adapter()` | 返回 `const proto_adapter_t *` 适配器实例 |
| `autogen_agent_def_destroy` / `autogen_agent_instance_destroy` / `autogen_group_chat_def_destroy` / `autogen_message_destroy` / `autogen_conversation_destroy` / `autogen_group_chat_result_destroy` | 释放对应结构体内部资源 |

## 用法示例

```c
#include "autogen_adapter.h"

autogen_config_t cfg = autogen_config_default();
autogen_adapter_context_t *ctx = autogen_adapter_create(&cfg);

/* 创建 Assistant 与 UserProxy 两个 Agent */
autogen_agent_def_t assistant = {
    .name = "assistant",
    .role = AGENT_ROLE_ASSISTANT,
    .system_message = "You are a helpful assistant.",
    .max_consecutive_auto_reply = 10,
};
char asst_id[64] = {0};
autogen_create_agent(ctx, &assistant, asst_id);

autogen_agent_def_t user_proxy = {
    .name = "user-proxy",
    .role = AGENT_ROLE_USER_PROXY,
    .human_input_mode = HUMAN_MODE_NEVER,
    .code_execution_enabled = true,
};
char user_id[64] = {0};
autogen_create_agent(ctx, &user_proxy, user_id);

/* 组建群聊并发起对话 */
char *participants[] = {asst_id, user_id};
autogen_group_chat_def_t chat = {
    .name = "team-chat",
    .mode = GROUP_CHAT_ROUND_ROBIN,
    .participant_ids = participants,
    .participant_count = 2,
    .max_rounds = 5,
};
char group_id[64] = {0};
autogen_create_group_chat(ctx, &chat, group_id);

autogen_group_chat_result_t result = {0};
autogen_initiate_chat(ctx, group_id, user_id, "Summarize the findings", &result);

/* 挂接代码执行与 LLM 回调 */
autogen_set_code_executor(ctx, my_code_executor, NULL);
autogen_set_llm_callback(ctx, my_llm_callback, NULL);

/* 清理 */
autogen_group_chat_result_destroy(&result);
autogen_adapter_destroy(ctx);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_AUTOGEN` 门控（默认 `ON`）；
关闭该选项后源码不参与编译。构建方式见
[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
