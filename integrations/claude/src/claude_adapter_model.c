// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file claude_adapter_model.c
 * @brief Claude adapter model catalog, model ID mapping and token estimation domain.
 *
 * Single responsibility: built-in model info table (g_builtin_models), model
 * enum to API name mapping, simple token estimation and model list queries.
 */

#define LOG_TAG "claude_adapter"

#include "claude_adapter.h"
#include "claude_adapter_internal.h"

#include "error.h"
#include "logging.h"
#include "airy_memory.h"
#include "protocol_transformers.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

claude_model_info_t g_builtin_models[] = {
    {
        .id = CLAUDE_MODEL_CLAUDE_3_5_SONNET,
        .api_name = "claude-3-5-sonnet-20241022",
        .display_name = "Claude 3.5 Sonnet",
        .max_context_tokens = 200000,
        .max_output_tokens = 8192,
        .supports_vision = true,
        .supports_extended_thinking = true,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 3.0,
        .cost_per_million_output = 15.0,
        .is_available = true,
    },
    {
        .id = CLAUDE_MODEL_CLAUDE_3_5_HAIKU,
        .api_name = "claude-3-5-haiku-20241022",
        .display_name = "Claude 3.5 Haiku",
        .max_context_tokens = 200000,
        .max_output_tokens = 8192,
        .supports_vision = true,
        .supports_extended_thinking = false,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 1.0,
        .cost_per_million_output = 5.0,
        .is_available = true,
    },
    {
        .id = CLAUDE_MODEL_CLAUDE_3_OPUS,
        .api_name = "claude-3-opus-20240229",
        .display_name = "Claude 3 Opus",
        .max_context_tokens = 200000,
        .max_output_tokens = 4096,
        .supports_vision = true,
        .supports_extended_thinking = false,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 15.0,
        .cost_per_million_output = 75.0,
        .is_available = true,
    },
    {
        .id = CLAUDE_MODEL_CLAUDE_3_SONNET,
        .api_name = "claude-3-sonnet-20240229",
        .display_name = "Claude 3 Sonnet",
        .max_context_tokens = 200000,
        .max_output_tokens = 4096,
        .supports_vision = true,
        .supports_extended_thinking = false,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 3.0,
        .cost_per_million_output = 15.0,
        .is_available = true,
    },
    {
        .id = CLAUDE_MODEL_CLAUDE_3_HAIKU,
        .api_name = "claude-3-haiku-20240307",
        .display_name = "Claude 3 Haiku",
        .max_context_tokens = 200000,
        .max_output_tokens = 4096,
        .supports_vision = false,
        .supports_extended_thinking = false,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 0.25,
        .cost_per_million_output = 1.25,
        .is_available = true,
    },
    {
        .id = CLAUDE_MODEL_CLAUDE_3_7_SONNET,
        .api_name = "claude-sonnet-4-20250514",
        .display_name = "Claude 4 Sonnet (3.7)",
        .max_context_tokens = 200000,
        .max_output_tokens = 16384,
        .supports_vision = true,
        .supports_extended_thinking = true,
        .supports_tool_use = true,
        .supports_prompt_caching = true,
        .cost_per_million_input = 3.0,
        .cost_per_million_output = 15.0,
        .is_available = true,
    },
};

const int g_builtin_model_count = sizeof(g_builtin_models) / sizeof(g_builtin_models[0]);

const char *claude_model_id_to_api_name(claude_model_id_t id)
{
    for (int i = 0; i < g_builtin_model_count; i++) {
        if (g_builtin_models[i].id == id)
            return g_builtin_models[i].api_name;
    }
    return "claude-3-5-sonnet-20241022";
}

int claude_estimate_tokens(const char *text)
{
    if (!text || !*text)
        return 0;
    int count = 0;
    bool in_word = false;
    for (const char *p = text; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '_' || (*p & 0x80)) {
            if (!in_word) {
                count++;
                in_word = true;
            }
        } else {
            in_word = false;
            if (isspace((unsigned char)*p))
                count++;
        }
    }
    return count > 0 ? count : 1;
}

int claude_count_tokens(claude_adapter_context_t *ctx, const claude_message_t *messages,
                        size_t message_count, const char *system_prompt, int *token_count)
{
    if (!ctx || !token_count)
        return AIRY_ERR_NULL_POINTER;
    if (!ctx->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    int total_chars = 0;
    if (system_prompt)
        total_chars += (int)strlen(system_prompt);
    for (size_t i = 0; i < message_count; i++) {
        if (messages[i].content)
            total_chars += (int)strlen(messages[i].content);
    }

    *token_count = (total_chars + 3) / 4;
    return 0;
}

int claude_list_models(claude_adapter_context_t *ctx, claude_model_info_t **models, size_t *count)
{
    if (!ctx || !models || !count)
        return AIRY_ERR_NULL_POINTER;

    *models = (claude_model_info_t *)AIRY_CALLOC((size_t)g_builtin_model_count,
                                                 sizeof(claude_model_info_t));
    if (!*models)
        return AIRY_ERR_OUT_OF_MEMORY;

    for (int i = 0; i < g_builtin_model_count; i++) {
        (*models)[i] = g_builtin_models[i];
        (*models)[i].api_name = AIRY_STRDUP(g_builtin_models[i].api_name);
        (*models)[i].display_name = AIRY_STRDUP(g_builtin_models[i].display_name);
    }

    *count = (size_t)g_builtin_model_count;
    return 0;
}
