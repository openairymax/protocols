/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file mcp_client.h
 * @brief MCP v1.0 client (consumes tools from external MCP servers).
 *
 * Client capability symmetric to mcp_v1_adapter.h (the MCP server role),
 * letting the AgentRT gateway (gateway_d) connect to external MCP servers
 * (e.g. @modelcontextprotocol/server-filesystem) and register the tools they
 * expose into the local MCP tool table ("gateway consumes external tools").
 *
 * Two transports (P2-4):
 *   1. stdio: fork+exec the external MCP server; stdin/stdout carries JSON-RPC
 *      2.0 frames (LSP-style Content-Length header + \r\n\r\n + JSON body);
 *      frame parsing is a state machine handling multiple/partial frames per read.
 *   2. http: basic MCP Streamable HTTP support, one short connection per request
 *      (POST <url>, Accept: application/json, text/event-stream).
 *
 * Thread safety: not required (used single-threaded by the gateway_d event loop).
 * Timeout: 60s by default (MCP_CLIENT_DEFAULT_TIMEOUT_MS).
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

#define MCP_CLIENT_DEFAULT_TIMEOUT_MS 60000
#define MCP_CLIENT_MAX_MESSAGE_SIZE (10 * 1024 * 1024)
#define MCP_CLIENT_PROTOCOL_VERSION "2024-11-05"
typedef enum {
    MCP_CLIENT_TRANSPORT_NONE = 0,
    MCP_CLIENT_TRANSPORT_STDIO = 1,
    MCP_CLIENT_TRANSPORT_HTTP = 2
} mcp_client_transport_t;


/* Reuses AIRY_ERR_* (error.h): TIMEOUT/IO/PARSE_ERROR/OUT_OF_MEMORY/
  * INVALID_PARAM/NOT_FOUND/NOT_SUPPORTED/SYS_SOCKET; the rest are client-specific */
#define MCP_CLIENT_ERR_BASE (-2000)
#define MCP_CLIENT_ERR_PROCESS_EXIT (MCP_CLIENT_ERR_BASE - 1)
#define MCP_CLIENT_ERR_RPC_ERROR (MCP_CLIENT_ERR_BASE - 2)
#define MCP_CLIENT_ERR_CONNECT (MCP_CLIENT_ERR_BASE - 3)
#define MCP_CLIENT_ERR_NOT_CONNECTED (MCP_CLIENT_ERR_BASE - 4)
#define MCP_CLIENT_ERR_FRAME (MCP_CLIENT_ERR_BASE - 5)


/**
  * @brief A single external tool description (parsed from tools/list results)
  * @note input_schema_json is the JSON text of the inputSchema object (cJSON-serialized),
  *       embeddable directly into a local MCP server's tools/list response.
 */
typedef struct {
    char *name;
    char *description;
    char *input_schema_json;
} mcp_client_tool_t;

typedef struct {
    mcp_client_tool_t *tools;
    size_t count;
} mcp_client_tool_list_t;

typedef struct mcp_client_s mcp_client_t;


/**
  * @brief Connect to an external MCP server over stdio
  * @param name    Client name (for logging and the gateway tool prefix)
  * @param command Executable path/command (e.g. "npx", "python3")
  * @param argv    Argument array (argv[0] should be command, NULL-terminated; deep-copied)
  * @return Client handle, or NULL on failure (reason logged)
 */
mcp_client_t *mcp_client_connect_stdio(const char *name, const char *command, char *const argv[]);

/**
  * @brief Connect to an external MCP server over HTTP (basic Streamable HTTP)
  * @param name Client name
  * @param url  Full endpoint URL, e.g. http://127.0.0.1:3001/mcp
 * @return Client handle, or NULL on failure
 */
mcp_client_t *mcp_client_connect_http(const char *name, const char *url);

/**
  * @brief Disconnect and free resources (stdio: terminate and reap the child)
  * @return 0 on success; otherwise an AIRY_ERR_* code
 */
int mcp_client_disconnect(mcp_client_t *client);


/**
  * @brief Ensure tools/list is called after initialize to fetch external tools
  * @param out Output tool list (caller frees via mcp_client_tool_list_free)
  * @return 0 on success; otherwise an MCP_CLIENT_ERR_* or AIRY_ERR_* code
 */
int mcp_client_list_tools(mcp_client_t *client, mcp_client_tool_list_t *out);

/**
  * @brief Call an external tool (tools/call)
  * @param name           Raw external tool name
  * @param arguments_json Arguments JSON object text (e.g. "{\"path\":\"/tmp\"}", may be "{}")
  * @param result_json    Output full JSON-RPC response text (AIRY_MALLOC; caller AIRY_FREE)
  * @return 0 on success (result_json non-NULL); otherwise an MCP_CLIENT_ERR_* or AIRY_ERR_* code
 */
int mcp_client_call_tool(mcp_client_t *client, const char *name, const char *arguments_json,
                         char **result_json);

/**
  * @brief Extract the first text content from a tools/call response (for gateway MCP replies)
  * @param response_json Response from mcp_client_call_tool
  * @param text_json     Output JSON string (quoted, e.g. "\"echo:hi\""; AIRY_MALLOC,
  *                      caller AIRY_FREE). On a top-level error, its message is output.
  * @return 0 on success; otherwise an AIRY_ERR_* code
 */
int mcp_client_extract_text(const char *response_json, char **text_json);

/**
  * @brief Free a tool list
 */
void mcp_client_tool_list_free(mcp_client_tool_list_t *list);

const char *mcp_client_transport_string(mcp_client_transport_t t);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_MCP_CLIENT_H */
