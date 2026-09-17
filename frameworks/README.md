# frameworks — 框架适配器

**位置：** `protocols/frameworks/` ｜ **版本：** 0.1.16
**上游文档：** [protocols 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

本目录收录将主流 Agent 框架的概念与调用模型映射到统一协议层的框架适配器。
当前包含两个适配器，各自提供独立的公开头文件、完整 C 实现与配套测试：

| 子目录 | 适配框架 | 覆盖概念 | 公开头文件 |
|--------|----------|----------|-----------|
| [`langchain/`](langchain/README.md) | LangChain | Chain / Agent / Tool / Memory / 流式输出 | `langchain_adapter.h` |
| [`autogen/`](autogen/README.md) | Microsoft AutoGen | 多代理对话 / 群聊 / 代码执行 / 人机协作 | `autogen_adapter.h` |

## 目录结构

```
frameworks/
├── README.md
├── langchain/
│   ├── include/langchain_adapter.h
│   └── src/                       # 6 个 .c + langchain_adapter_internal.h
└── autogen/
    ├── include/autogen_adapter.h
    └── src/                       # 4 个 .c + autogen_adapter_internal.h
```

## 编译门控

两个适配器均由独立 CMake 选项门控，默认启用；关闭后对应源码不参与编译，
头文件亦不安装。

| CMake 选项 | 默认值 | 门控目标 |
|------------|--------|----------|
| `PROTOCOLS_ENABLE_LANGCHAIN` | `ON` | `langchain/` |
| `PROTOCOLS_ENABLE_AUTOGEN` | `ON` | `autogen/` |

## 依赖

| 依赖 | 来源 | 用途 |
|------|------|------|
| `airy_protocol_interface.h` | `protocols/include/` | 协议适配器接口（`proto_adapter_t` 虚表） |
| `unified_protocol.h` | `protocols/include/` | 统一消息模型 |

两个适配器分别通过 `langchain_get_protocol_adapter()` / `autogen_get_protocol_adapter()`
向协议注册表暴露统一的 `proto_adapter_t` 实例。

## 相关

- 各适配器的常量、枚举、API 与用法示例见对应子目录 README。
- 构建方式与选项总表见[主文档「构建」一节](../README_zh.md#构建)。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
