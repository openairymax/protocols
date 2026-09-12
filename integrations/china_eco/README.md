# 国内生态适配器

**位置：** `protocols/integrations/china_eco/` ｜ **版本：** 0.1.15
**上游文档：** [protocols 主文档（中文）](../../README_zh.md) ｜ [English](../../README.md)

## 概述

本目录实现国内生态兼容适配器（`CHINA_ECO_VERSION` 为 `"0.1.0"`），
提供三组已落地的桥接能力：LLM Provider 桥（8 家国内模型平台，
OpenAI 兼容调用形态）、对象存储桥（阿里云 OSS / 腾讯云 COS / 百度云
BOS / 华为云 OBS 四类），以及国密算法套件——SM3 哈希按
GB/T 32905-2016、SM4 分组密码按 GB/T 32907-2016（CBC 模式 + PKCS#7
填充）为本目录原生实现。能力位定义另含 MESSAGE_QUEUE 与
CONTENT_AUDIT 两项（作为能力映射保留位，本目录未提供对应公开 API）。

## 目录结构

```
china_eco/
├── include/
│   └── china_eco_adapter.h     # 公开头文件
└── src/
    ├── china_eco_adapter.c     # 句柄、vtable、Provider/存储注册
    ├── china_eco_llm.c         # LLM 桥（OpenAI 兼容请求构造）
    ├── china_eco_crypto.c      # SM3 / SM4 原生实现
    └── china_eco_internal.h    # 内部结构
```

## 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `CHINA_ECO_VERSION` | `"0.1.0"` | 适配器版本 |
| `CHINA_ECO_PROTOCOL_NAME` | `"china-eco"` | 协议名 |
| `CHINA_ECO_MAX_PROVIDERS` | 16 | Provider 注册容量 |
| `CHINA_ECO_MAX_ENDPOINTS` | 64 | 端点容量 |
| `CHINA_ECO_MAX_MESSAGE_SIZE` | 16 MB | 单条消息上限 |
| `CHINA_ECO_DEFAULT_TIMEOUT_MS` | 30000 | 默认超时（毫秒） |
| `CHINA_ECO_SM2_KEY_SIZE` | 64 | SM2 公私钥缓冲区长度 |
| `CHINA_ECO_SM3_HASH_SIZE` / `CHINA_ECO_SM3_DIGEST_SIZE` | 32 / 32 | SM3 摘要长度 |
| `CHINA_ECO_SM4_BLOCK_SIZE` / `CHINA_ECO_SM4_KEY_SIZE` / `CHINA_ECO_SM4_IV_SIZE` | 16 / 16 / 16 | SM4 分组/密钥/IV 长度 |

## 枚举

### LLM Provider（`china_eco_provider_type_t`，8 家）

| 枚举 | 值 | 平台 |
|------|----|------|
| `CHINA_ECO_PROVIDER_BAILIAN` | 0 | 阿里云百炼 |
| `CHINA_ECO_PROVIDER_WENXIN` | 1 | 百度文心 |
| `CHINA_ECO_PROVIDER_DASHSCOPE` | 2 | 阿里云 DashScope |
| `CHINA_ECO_PROVIDER_ZHIPU` | 3 | 智谱 AI |
| `CHINA_ECO_PROVIDER_MINIMAX` | 4 | MiniMax |
| `CHINA_ECO_PROVIDER_MOONSHOT` | 5 | 月之暗面 |
| `CHINA_ECO_PROVIDER_DEEPSEEK` | 6 | 深度求索 |
| `CHINA_ECO_PROVIDER_QWEN` | 7 | 通义千问（开源模型） |

### 对象存储（`china_eco_storage_type_t`，4 类）

`CHINA_ECO_OSS_ALIYUN`（0）、`CHINA_ECO_OSS_TENCENT`（1）、
`CHINA_ECO_OSS_BAIDU`（2）、`CHINA_ECO_OSS_HUAWEI`（3）

### 能力位（`china_eco_capability_t`）

`CHINA_ECO_CAP_LLM_BRIDGE`（0x01）、
`CHINA_ECO_CAP_OBJECT_STORAGE`（0x02）、`CHINA_ECO_CAP_SM_CRYPTO`（0x04）、
`CHINA_ECO_CAP_MESSAGE_QUEUE`（0x08，保留位）、
`CHINA_ECO_CAP_CONTENT_AUDIT`（0x10，保留位）

## 主要类型

| 类型 | 说明 |
|------|------|
| `china_eco_llm_provider_t` | Provider 条目（类型、端点、密钥等） |
| `china_eco_storage_bridge_t` | 存储桥条目（存储类型、端点、凭证） |
| `china_eco_sm2_context_t` | SM2 密钥上下文（64 字节公钥/私钥缓冲） |
| `china_eco_sm4_context_t` | SM4 加解密上下文（16 字节密钥 + IV） |
| `china_eco_handle_t` | 适配器句柄（Provider/存储表与计数器） |

## 核心 API

| 函数 | 说明 |
|------|------|
| `china_eco_create(&handle)` / `china_eco_destroy(handle)` | 句柄生命周期 |
| `china_eco_add_llm_provider(h, provider)` / `china_eco_remove_llm_provider(h, type)` | Provider 注册与移除 |
| `china_eco_llm_chat(h, provider, ...)` | 经指定 Provider 发起对话请求 |
| `china_eco_add_storage_bridge(h, bridge)` | 注册对象存储桥 |
| `china_eco_storage_upload` / `china_eco_storage_download` | 按存储类型上传/下载对象 |
| `china_eco_sm3_hash(data, size, digest[32])` | SM3 摘要（GB/T 32905-2016） |
| `china_eco_sm4_encrypt` / `china_eco_sm4_decrypt` | SM4-CBC + PKCS#7 加解密（GB/T 32907-2016） |

## 用法示例

```c
#include "china_eco_adapter.h"

china_eco_handle_t *h = NULL;
china_eco_create(&h);

china_eco_llm_provider_t p = {0};
p.type = CHINA_ECO_PROVIDER_DEEPSEEK;
/* ... 填充端点与凭证 ... */
china_eco_add_llm_provider(h, &p);

uint8_t digest[CHINA_ECO_SM3_DIGEST_SIZE];
china_eco_sm3_hash("hello", 5, digest);

china_eco_sm4_context_t sm4 = {{0}, {0}};
/* 填入 16 字节 key / iv 后： */
/* china_eco_sm4_encrypt(&sm4, pt, pt_len, ct, &ct_len); */

china_eco_destroy(h);
```

## 构建

本目录由 CMake 选项 `PROTOCOLS_ENABLE_CHINA_ECO` 门控（默认 `ON`）；
Windows 上强制 `OFF`。构建方式见
[主文档「构建」一节](../../README_zh.md#构建)。
国密算法测试位于仓库 `tests/` 目录（`test_china_eco_crypto.c`）。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../../LICENSE)，版权与商标声明见
[NOTICE](../../NOTICE)。
