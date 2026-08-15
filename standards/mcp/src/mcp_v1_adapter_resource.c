// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file mcp_v1_adapter_resource.c
 * @brief MCP v1.0 resource domain (resources/list, resources/read, resources/templates/list).
 */

#include "mcp_v1_adapter_internal.h"

#include "mcp_transport.h"
#include "airy_memory.h"
#include "error.h"
#include "types.h"
#include "unified_protocol.h"

#include <cjson/cJSON.h>

#include <cjson_helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logging.h"

int mcp_v1_handle_resources_list(mcp_v1_context_t *ctx, char **response_json)
{
    if (!ctx || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_resources_list: failed");
        return AIRY_ERR_UNKNOWN;
    }

    size_t buf_size = 4096 + ctx->resource_count * 512;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "{\"resources\":[");

    for (size_t i = 0; i < ctx->resource_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        char *uri = json_string_escape(ctx->resources[i].resource.uri);
        char *name = json_string_escape(ctx->resources[i].resource.name);
        char *desc = json_string_escape(ctx->resources[i].resource.description);
        char *mime = json_string_escape(ctx->resources[i].resource.mime_type);
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"uri\":%s,\"name\":%s,\"description\":%s,\"mimeType\":%s}", uri, name,
                           desc, mime);
        AIRY_FREE(uri);
        AIRY_FREE(name);
        AIRY_FREE(desc);
        AIRY_FREE(mime);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;
    return 0;
}

int mcp_v1_handle_resources_read(mcp_v1_context_t *ctx, const char *uri, char **response_json)
{
    if (!ctx || !uri || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_resources_read: failed");
        return AIRY_ERR_UNKNOWN;
    }

    mcp_resource_entry_t *found = NULL;
    for (size_t i = 0; i < ctx->resource_count; i++) {
        if (strcmp(ctx->resources[i].resource.uri, uri) == 0) {
            found = &ctx->resources[i];
            break;
        }
    }

    if (!found || !found->handler) {
        AIRY_LOG_WARN("resource not found or no handler: uri=%s, resource_count=%zu", uri,
                 ctx->resource_count);
        char *uri_esc = json_string_escape(uri);
        size_t len =
            snprintf(NULL, 0, "{\"contents\":[{\"uri\":%s,\"text\":\"Resource not found\"}]}",
                     uri_esc);
        char *resp = AIRY_MALLOC(len + 1);
        if (resp)
            snprintf(resp, len + 1, "{\"contents\":[{\"uri\":%s,\"text\":\"Resource not found\"}]}",
                     uri_esc);
        AIRY_FREE(uri_esc);
        *response_json = resp;
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: invalid parameter");
        return AIRY_ERR_INVALID_PARAM;
    }

    char *content = NULL;
    char *mime_type = NULL;
    found->handler(uri, &content, &mime_type, found->user_data);

    char *uri_esc = json_string_escape(uri);
    char *content_esc = json_string_escape(content);
    char *mime_esc = json_string_escape(mime_type);

    size_t len = snprintf(NULL, 0, "{\"contents\":[{\"uri\":%s,\"mimeType\":%s,\"text\":%s}]}",
                          uri_esc, mime_esc, content_esc);
    char *resp = AIRY_MALLOC(len + 1);
    if (resp)
        snprintf(resp, len + 1, "{\"contents\":[{\"uri\":%s,\"mimeType\":%s,\"text\":%s}]}",
                 uri_esc, mime_esc, content_esc);

    AIRY_FREE(uri_esc);
    AIRY_FREE(content_esc);
    AIRY_FREE(mime_esc);
    AIRY_FREE(content);
    AIRY_FREE(mime_type);
    *response_json = resp;
    return 0;
}

int mcp_v1_handle_resources_templates(mcp_v1_context_t *ctx, char **response_json)
{
    if (!ctx || !response_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "mcp_v1_handle_resources_templates: failed");
        return AIRY_ERR_UNKNOWN;
    }

    size_t buf_size = 4096 + ctx->template_count * 512;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                         "mcp_v1_adapter: null pointer");
        return AIRY_ERR_NULL_POINTER;
    }

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "{\"resourceTemplates\":[");

    for (size_t i = 0; i < ctx->template_count; i++) {
        if (i > 0)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        char *uri = json_string_escape(ctx->resource_templates[i].uri_template);
        char *name = json_string_escape(ctx->resource_templates[i].name);
        char *desc = json_string_escape(ctx->resource_templates[i].description);
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"uriTemplate\":%s,\"name\":%s,\"description\":%s}", uri, name, desc);
        AIRY_FREE(uri);
        AIRY_FREE(name);
        AIRY_FREE(desc);
    }

    offset += snprintf(buf + offset, buf_size - offset, "]}");
    *response_json = buf;
    return 0;
}
