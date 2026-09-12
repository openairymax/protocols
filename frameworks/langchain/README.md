# langchain — LangChain 框架适配器

**位置：** `protocols/frameworks/langchain/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现 LangChain 框架适配器：将 LangChain 的 Chain、Agent、Tool、Memory
等核心对象模型映射到统一协议层，支持链式执行（含流式）、多步推理代理运行、
工具注册与调用、对话记忆管理，以及 LLM 调用、追踪、流式输出等回调挂接。
适配器同时以 `proto_adapter_t` 虚表形式接入协议注册表。

## 目录结构

```
langchain/
├── README.md
├── include/
│   └── langchain_adapter.h            # 公开头文件
└── src/
    ├── langchain_adapter.c            # 适配器生命周期与配置
    ├── langchain_adapter_chain.c      # Chain 创建/编译/执行
    ├── langchain_adapter_agent.c      # Agent 创建与运行
    ├── langchain_adapter_tool.c       # Tool 注册与调用
    ├── langchain_adapter_memory.c     # Memory 创建与读写
    ├── langchain_adapter_proto.c      # proto_adapter_t 虚表接入
    └── langchain_adapter_internal.h   # 内部共享声明
```

配套测试：`tests/test_langchain_adapter.c`。

## 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `LANGCHAIN_ADAPTER_VERSION` | `"1.0.0"` | 适配器版本字符串 |
| `LANGCHAIN_MAX_CHAINS` | 64 | 上下文内 Chain 槽位上限 |
| `LANGCHAIN_MAX_TOOLS` | 128 | 上下文内 Tool 槽位上限 |
| `LANGCHAIN_MAX_AGENTS` | 32 | 上下文内 Agent 槽位上限 |
| `LANGCHAIN_MAX_MEMORY_ENTRIES` | 1024 | 上下文内 Memory 实例上限 |
| `LANGCHAIN_DEFAULT_TIMEOUT_MS` | 60000 | 默认超时（毫秒） |

## 枚举

组件类型 `langchain_component_type_t`：
`LC_TYPE_LLM`、`LC_TYPE_CHAT_MODEL`、`LC_TYPE_EMBEDDING_MODEL`、`LC_TYPE_TOOL`、
`LC_TYPE_CHAIN`、`LC_TYPE_AGENT`、`LC_TYPE_MEMORY`、`LC_TYPE_RETRIEVER`、
`LC_TYPE_OUTPUT_PARSER`。

Chain 类型 `langchain_chain_type_t`：
`LC_CHAIN_SEQUENTIAL`、`LC_CHAIN_ROUTER`、`LC_CHAIN_MAP_REDUCE`、
`LC_CHAIN_PARALLEL`、`LC_CHAIN_CONDITIONAL`、`LC_CHAIN_CUSTOM`。

Agent 类型 `langchain_agent_type_t`：
`LC_AGENT_REACT`、`LC_AGENT_PLAN_AND_EXECUTE`、`LC_AGENT_OPENAI_FUNCTIONS`、
`LC_AGENT_STRUCTURED_CHAT`、`LC_AGENT_XML`。

Memory 类型 `langchain_memory_type_t`：
`LC_MEM_BUFFER`、`LC_MEM_SUMMARY`、`LC_MEM_WINDOW`、`LC_MEM_TOKEN`、
`LC_MEM_ENTITY`、`LC_MEM_KG`。

## 主要类型

| 类型 | 说明 |
|------|------|
| `langchain_config_t` | 适配器配置（base URL、API Key、超时、流式/追踪/缓存开关、默认模型等） |
| `langchain_adapter_context_t` | 适配器上下文（公开结构，内含固定容量的 Chain/Tool/Agent/Memory 数组、回调与统计字段） |
| `langchain_chain_def_t` / `langchain_chain_instance_t` | Chain 定义与编译后的实例 |
| `langchain_agent_def_t` / `langchain_agent_instance_t` | Agent 定义与运行实例 |
| `langchain_tool_def_t` | Tool 定义（名称、描述、JSON Schema、异步标志） |
| `langchain_memory_t` | Memory 实例（类型、条目上限、消息列表、摘要） |
| `langchain_execution_result_t` | 执行结果（输入/输出 JSON、耗时、步骤数、中间结果） |
| `langchain_tool_executor_fn` | 工具执行回调：`int (*)(tool_name, input_json, char **output_json, user_data)` |
| `langchain_streaming_fn` / `langchain_trace_fn` / `langchain_llm_callback_fn` | 流式分块、追踪事件、LLM 调用回调 |

## 核心 API

### 生命周期

| 函数 | 说明 |
|------|------|
| `langchain_config_default()` | 返回默认配置（按值返回） |
| `langchain_adapter_create(config)` | 创建适配器上下文 |
| `langchain_adapter_destroy(ctx)` | 销毁适配器上下文 |
| `langchain_adapter_is_initialized(ctx)` | 查询初始化状态 |
| `langchain_adapter_version()` | 返回版本字符串 |

### Chain / Agent / Tool / Memory

| 函数 | 说明 |
|------|------|
| `langchain_create_chain(ctx, definition, instance)` | 按定义创建 Chain 实例（输出结构体） |
| `langchain_execute_chain(ctx, chain_id, input_json, result)` | 同步执行 Chain |
| `langchain_execute_chain_streaming(ctx, chain_id, input_json, stream_handler, user_data)` | 流式执行 Chain |
| `langchain_create_agent(ctx, definition, out_agent_id)` | 创建 Agent，输出 ID 写入调用方缓冲 |
| `langchain_agent_run(ctx, agent_id, task_input, result)` | 运行 Agent |
| `langchain_register_tool(ctx, tool, executor, user_data)` | 注册工具及执行回调 |
| `langchain_list_tools(ctx, tools, count)` | 列出已注册工具 |
| `langchain_create_memory(ctx, type, max_entries, out_memory)` | 创建 Memory 实例 |
| `langchain_memory_add(ctx, memory_id, role, content)` | 追加记忆条目 |
| `langchain_memory_get(ctx, memory_id, snapshot)` | 获取 Memory 快照 |

### 回调与统计

| 函数 | 说明 |
|------|------|
| `langchain_set_streaming_handler(ctx, handler, user_data)` | 设置流式输出回调 |
| `langchain_set_trace_handler(ctx, handler, user_data)` | 设置追踪回调 |
| `langchain_set_llm_callback(ctx, callback, user_data)` | 设置 LLM 调用回调 |
| `langchain_get_statistics(ctx, stats_json, buffer_size)` | 导出 JSON 格式统计 |

### 协议接入与实体释放

| 函数 | 说明 |
|------|------|
| `langchain_get_protocol_adapter()` | 返回 `const proto_adapter_t *` 适配器实例 |
| `langchain_tool_def_destroy` / `langchain_chain_def_destroy` / `langchain_chain_instance_destroy` / `langchain_agent_def_destroy` / `langchain_memory_destroy` / `langchain_execution_result_destroy` | 释放对应结构体内部资源 |

## 用法示例

```c
#include "langchain_adapter.h"

static int calculator_executor(const char *tool_name, const char *input_json,
                               char **output_json, void *user_data)
{
    *output_json = strdup("{\"result\":42}");
    return 0;
}

langchain_config_t cfg = langchain_config_default();
langchain_adapter_context_t *ctx = langchain_adapter_create(&cfg);

/* 注册工具 */
langchain_tool_def_t tool = {
    .name = "calculator",
    .description = "Performs arithmetic calculations",
    .function_schema_json = "{\"type\":\"object\"}",
    .tool_type = LC_TYPE_TOOL,
};
langchain_register_tool(ctx, &tool, calculator_executor, NULL);

/* 创建并执行 Chain */
char *steps[] = {"retriever", "llm"};
langchain_chain_def_t cdef = {
    .name = "qa-chain",
    .type = LC_CHAIN_SEQUENTIAL,
    .step_ids = steps,
    .step_count = 2,
};
langchain_chain_instance_t chain = {0};
langchain_create_chain(ctx, &cdef, &chain);

langchain_execution_result_t result = {0};
langchain_execute_chain(ctx, chain.id, "{\"query\":\"What is AI?\"}", &result);

/* 创建 Memory 并写入对话 */
langchain_memory_t mem = {0};
langchain_create_memory(ctx, LC_MEM_BUFFER, 100, &mem);
langchain_memory_add(ctx, mem.id, "user", "Hello!");

/* 挂接流式回调 */
langchain_set_streaming_handler(ctx, my_stream_handler, NULL);

/* 接入统一协议注册表 */
const proto_adapter_t *adapter = langchain_get_protocol_adapter();

/* 清理 */
langchain_execution_result_destroy(&result);
langchain_adapter_destroy(ctx);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_LANGCHAIN` 门控（默认 `ON`）；
关闭该选项后源码不参与编译。构建方式见
[主文档「构建」一节](../../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
