// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file langchain_adapter_agent.c
 * @brief LangChain adapter agent execution domain.
 *
 * Single responsibility: agent instance creation (langchain_create_agent) and
 * agent task execution (langchain_agent_run).
 */

#define LOG_TAG "langchain_adapter"

#include "langchain_adapter.h"
#include "langchain_adapter_internal.h"

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int langchain_create_agent(langchain_adapter_context_t *ctx,
                           const langchain_agent_def_t *definition, char *out_agent_id)
{
    if (!ctx || !out_agent_id)
        return AIRY_ERR_NULL_POINTER;
    if (ctx->agent_count >= LANGCHAIN_MAX_AGENTS)
        return AIRY_ERR_OVERFLOW;

    static uint32_t agent_counter = 0;
    agent_counter++;
    snprintf(out_agent_id, 64, "lc-agent-%08x", agent_counter);

    langchain_agent_instance_t *agent = &ctx->agents[ctx->agent_count];
    AIRY_MEMSET(agent, 0, sizeof(*agent));
    agent->id = AIRY_STRDUP(out_agent_id);

    if (definition) {
        agent->name = definition->name ? AIRY_STRDUP(definition->name) : NULL;
    }

    agent->is_available = true;
    ctx->agent_count++;

    return 0;
}

int langchain_agent_run(langchain_adapter_context_t *ctx, const char *agent_id,
                        const char *task_input, langchain_execution_result_t *result)
{
    if (!ctx || !result)
        return AIRY_ERR_NULL_POINTER;
    if (!ctx->llm_callback)
        return AIRY_ERR_OVERFLOW;

    langchain_agent_instance_t *found = NULL;
    if (agent_id) {
        for (size_t i = 0; i < ctx->agent_count; i++) {
            if (ctx->agents[i].id && strcmp(ctx->agents[i].id, agent_id) == 0) {
                found = &ctx->agents[i];
                break;
            }
        }
    }

    ctx->total_chains_executed++;

    AIRY_MEMSET(result, 0, sizeof(langchain_execution_result_t));
    result->chain_id = agent_id ? AIRY_STRDUP(agent_id) : NULL;
    result->input_json = task_input ? AIRY_STRDUP(task_input) : NULL;

    time_t start = time(NULL);

    char resp_text[LC_MAX_RESPONSE_LEN];
    AIRY_MEMSET(resp_text, 0, sizeof(resp_text));
    size_t tool_cnt = (found && found->tool_count > 0) ? found->tool_count : ctx->tool_count;
    int rc =
        lc_generate_chain_response(ctx, task_input, tool_cnt, true, resp_text, sizeof(resp_text));

    if (rc == 0 && resp_text[0]) {
        int input_tokens = lc_word_count(task_input);
        int output_tokens = lc_word_count(resp_text);

        char output_buf[LC_MAX_RESPONSE_LEN + 256];
        snprintf(output_buf, sizeof(output_buf),
                 "{\"agent_response\":\"%.1800s\","
                 "\"reasoning_steps\":%d,\"tools_used\":%zu,"
                 "\"input_tokens\":%d,\"output_tokens\":%d,"
                 "\"model\":\"%s\"}",
                 resp_text, (int)(tool_cnt > 0 ? tool_cnt + 2 : 3), tool_cnt, input_tokens,
                 output_tokens,
                 ctx->config.default_llm_model ? ctx->config.default_llm_model : "gpt-4o");
        result->output_json = AIRY_STRDUP(output_buf);
    } else {
        result->output_json =
            AIRY_STRDUP("{\"status\":\"error\",\"message\":\"LLM callback unavailable\"}");
        result->success = false;
        result->error_message = AIRY_STRDUP("No LLM callback configured");
        return AIRY_ERR_OVERFLOW;
    }

    result->execution_time_ms = difftime(time(NULL), start) * 1000.0;
    if (result->execution_time_ms < 1.0)
        result->execution_time_ms = 1.0;
    result->step_count = (int)(tool_cnt > 0 ? tool_cnt + 2 : 3);
    result->success = true;

    ctx->total_chains_executed++;
    return 0;
}
