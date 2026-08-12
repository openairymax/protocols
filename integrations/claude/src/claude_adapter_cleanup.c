// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file claude_adapter_cleanup.c
 * @brief Claude adapter common resource-release domain.
 *
 * Single responsibility: deep-free claude_response_t / claude_message_t /
 * claude_tool_def_t / claude_model_info_t / claude_stream_event_t.
 */

#define LOG_TAG "claude_adapter"

#include "claude_adapter.h"
#include "claude_adapter_internal.h"

#include "airy_memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void claude_response_destroy(claude_response_t *resp)
{
    if (!resp)
        return;
    AIRY_FREE(resp->id);
    AIRY_FREE(resp->model);
    for (size_t i = 0; i < resp->block_count; i++) {
        if (resp->content_blocks[i].type) {
            if (strcmp(resp->content_blocks[i].type, "text") == 0)
                AIRY_FREE(resp->content_blocks[i].content.text);
            else if (strcmp(resp->content_blocks[i].type, "tool_use") == 0) {
                AIRY_FREE(resp->content_blocks[i].content.tool_use.id);
                AIRY_FREE(resp->content_blocks[i].content.tool_use.name);
                AIRY_FREE(resp->content_blocks[i].content.tool_use.input_json);
            } else if (strcmp(resp->content_blocks[i].type, "tool_result") == 0) {
                AIRY_FREE(resp->content_blocks[i].content.tool_result.tool_use_id);
                AIRY_FREE(resp->content_blocks[i].content.tool_result.content);
            }
            AIRY_FREE(resp->content_blocks[i].type);
        }
    }
    AIRY_FREE(resp->content_blocks);
    AIRY_MEMSET(resp, 0, sizeof(claude_response_t));
}

void claude_message_destroy(claude_message_t *msg)
{
    if (!msg)
        return;
    AIRY_FREE(msg->id);
    AIRY_FREE(msg->content);
    for (size_t i = 0; i < msg->breakpoint_count; i++)
        AIRY_FREE(msg->cache_control_breakpoints[i]);
    AIRY_FREE(msg->cache_control_breakpoints);
    AIRY_MEMSET(msg, 0, sizeof(claude_message_t));
}

void claude_tool_def_destroy(claude_tool_def_t *tool)
{
    if (!tool)
        return;
    AIRY_FREE(tool->name);
    AIRY_FREE(tool->description);
    AIRY_FREE(tool->input_schema_json);
    AIRY_MEMSET(tool, 0, sizeof(claude_tool_def_t));
}

void claude_model_info_destroy(claude_model_info_t *info)
{
    if (!info)
        return;
    AIRY_FREE(info->api_name);
    AIRY_FREE(info->display_name);
    AIRY_MEMSET(info, 0, sizeof(claude_model_info_t));
}

void claude_stream_event_destroy(claude_stream_event_t *event)
{
    if (!event)
        return;
    AIRY_FREE(event->text);
    AIRY_MEMSET(event, 0, sizeof(claude_stream_event_t));
}
