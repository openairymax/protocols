// SPDX-FileCopyrightText: 2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
// @owner: team-B
/**
 * @file mcp_client.h
 * @brief MCP v1.0 Client（消费外部 MCP server 的工具）
 *
 * 与 mcp_v1_adapter.h（MCP server 角色）对称的客户端能力，使 AgentRT 网关
 * （gateway_d）能够连接外部 MCP server（如 @modelcontextprotocol/server-filesystem）
 * 并把它暴露的工具注册进本地 MCP 工具表，实现"网关消费外部工具"。
 *
 * 支持两种传输（P2-4）：
 *   1. stdio：fork+exec 外部 MCP server，stdin/stdout 承载 JSON-RPC 2.0 帧
 *      （LSP 风格 Content-Length 头 + \r\n\r\n + JSON body），帧解析为状态机，
 *      可处理一次 read 多帧/半帧情况。
 *   2. http：MCP Streamable HTTP 基础支持，每次请求建立短连接
 *      （POST <url>，Accept: application/json, text/event-stream）。
 *
 * 线程安全：不做要求（gateway_d 事件循环单线程使用）。
 * 超时：默认 60s（MCP_CLIENT_DEFAULT_TIMEOUT_MS）。
 *
 * @since 0.1.0
 * @see mcp_v1_adapter.h
 */

#ifndef AIRY_RT_MCP_CLIENT_H
#define AIRY_RT_MCP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCP_CLIENT_DEFAULT_TIMEOUT_MS 60000 /* 单次请求/响应总超时 */
#define MCP_CLIENT_MAX_MESSAGE_SIZE (10 * 1024 * 1024) /* 与 MCP_V1_MAX_MESSAGE_SIZE 对齐 */
#define MCP_CLIENT_PROTOCOL_VERSION "2024-11-05" /* 协商的协议版本 */

typedef enum {
    MCP_CLIENT_TRANSPORT_NONE = 0,
    MCP_CLIENT_TRANSPORT_STDIO = 1, /* 子进程 stdin/stdout 帧 */
    MCP_CLIENT_TRANSPORT_HTTP = 2   /* Streamable HTTP 短连接 */
} mcp_client_transport_t;

/* ==================== MCP 客户端错误码 ==================== */
/* 复用 AIRY_ERR_*（error.h）：AIRY_ERR_TIMEOUT/IO/PARSE_ERROR/OUT_OF_MEMORY/
 * INVALID_PARAM/NOT_FOUND/NOT_SUPPORTED/SYS_SOCKET 等；以下为客户端专属错误 */
#define MCP_CLIENT_ERR_BASE (-2000)
#define MCP_CLIENT_ERR_PROCESS_EXIT (MCP_CLIENT_ERR_BASE - 1) /* stdio 子进程退出/EOF */
#define MCP_CLIENT_ERR_RPC_ERROR (MCP_CLIENT_ERR_BASE - 2)    /* 远端返回 JSON-RPC error */
#define MCP_CLIENT_ERR_CONNECT (MCP_CLIENT_ERR_BASE - 3)      /* 连接/握手失败 */
#define MCP_CLIENT_ERR_NOT_CONNECTED (MCP_CLIENT_ERR_BASE - 4) /* 客户端未连接 */
#define MCP_CLIENT_ERR_FRAME (MCP_CLIENT_ERR_BASE - 5)        /* 帧协议错误（坏 Content-Length 等） */

/* ==================== 工具列表 ==================== */

/**
 * @brief 单个外部工具描述（从 tools/list 结果解析）
 * @note input_schema_json 为 inputSchema 对象的 JSON 文本（cJSON 序列化），
 *       可直接内嵌到本地 MCP server 的 tools/list 响应。
 */
typedef struct {
    char *name;              /* 工具名 */
    char *description;       /* 描述（可能为 NULL） */
    char *input_schema_json; /* inputSchema JSON 文本（可能为 NULL） */
} mcp_client_tool_t;

typedef struct {
    mcp_client_tool_t *tools;
    size_t count;
} mcp_client_tool_list_t;

typedef struct mcp_client_s mcp_client_t;

/* ==================== 连接生命周期 ==================== */

/**
 * @brief 以 stdio 传输连接外部 MCP server
 * @param name    客户端名（用于日志与 gateway 工具前缀）
 * @param command 可执行程序路径/命令名（如 "npx"、"python3"）
 * @param argv    参数数组（argv[0] 应为 command，NULL 结尾；内部深拷贝）
 * @return 客户端句柄；失败返回 NULL（日志已记录原因）
 */
mcp_client_t *mcp_client_connect_stdio(const char *name, const char *command, char *const argv[]);

/**
 * @brief 以 HTTP 传输连接外部 MCP server（Streamable HTTP 基础支持）
 * @param name 客户端名
 * @param url  完整端点 URL，如 http://127.0.0.1:3001/mcp
 * @return 客户端句柄；失败返回 NULL
 */
mcp_client_t *mcp_client_connect_http(const char *name, const char *url);

/**
 * @brief 断开连接并释放资源（stdio：终止并回收子进程）
 * @return 0 成功；否则 AIRY_ERR_* 错误码
 */
int mcp_client_disconnect(mcp_client_t *client);

/* ==================== 工具发现与调用 ==================== */

/**
 * @brief 确保 initialize 后调用 tools/list，拉取外部工具列表
 * @param out 输出的工具列表（调用方用 mcp_client_tool_list_free 释放）
 * @return 0 成功；否则 MCP_CLIENT_ERR_* 或 AIRY_ERR_* 错误码
 */
int mcp_client_list_tools(mcp_client_t *client, mcp_client_tool_list_t *out);

/**
 * @brief 调用外部工具（tools/call）
 * @param name           外部工具原始名
 * @param arguments_json 参数 JSON 对象文本（如 "{\"path\":\"/tmp\"}"，可为 "{}"）
 * @param result_json    输出的完整 JSON-RPC 响应文本（AIRY_MALLOC，调用方 AIRY_FREE）
 * @return 0 成功（result_json 非 NULL）；否则 MCP_CLIENT_ERR_* 或 AIRY_ERR_* 错误码
 */
int mcp_client_call_tool(mcp_client_t *client, const char *name, const char *arguments_json,
                         char **result_json);

/**
 * @brief 从 tools/call 响应中提取首个 text content（供 gateway 组装 MCP 响应）
 * @param response_json mcp_client_call_tool 的响应
 * @param text_json     输出的 JSON 字符串（带引号，如 "\"echo:hi\""；AIRY_MALLOC，
 *                      调用方 AIRY_FREE）。顶层 error 时输出其 message。
 * @return 0 成功；否则 AIRY_ERR_* 错误码
 */
int mcp_client_extract_text(const char *response_json, char **text_json);

/**
 * @brief 释放工具列表
 */
void mcp_client_tool_list_free(mcp_client_tool_list_t *list);

const char *mcp_client_transport_string(mcp_client_transport_t t);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_MCP_CLIENT_H */
