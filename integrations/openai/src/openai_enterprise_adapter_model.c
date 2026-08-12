// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_model.c
 * @brief OpenAI enterprise adapter model management domain (built-in model registration/model list/release).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/include/error.h"
#include "error.h"

#include <string.h>

/* ============================================================================
 * Model Management
 * ============================================================================ */

void openai_register_builtin_models(struct openai_enterprise_adapter_s *a)
{
    static const char *builtin[][4] = {
        {"gpt-4o", "GPT-4o", "Multimodal flagship model",
         "{\"modality\":[\"text\",\"image\"],\"context\":128000,\"training\":\"Apr2024\"}"},
        {"gpt-4o-mini", "GPT-4o Mini", "Efficient small model",
         "{\"modality\":[\"text\"],\"context\":128000,\"training\":\"Jul2024\"}"},
        {"gpt-4-turbo", "GPT-4 Turbo", "High-performance model",
         "{\"modality\":[\"text\"],\"context\":128000,\"training\":\"Nov2023\"}"},
        {"text-embedding-ada-002", "Text Embedding Ada 002",
         "Vector embedding model for text similarity",
         "{\"type\":\"embedding\",\"dimensions\":1536,\"max_tokens\":8191}"},
        {"text-embedding-3-small", "Text Embedding 3 Small", "Compact embedding model",
         "{\"type\":\"embedding\",\"dimensions\":1536,\"max_tokens\":8191}"},
        {"text-embedding-3-large", "Text Embedding 3 Large", "High-dimensional embedding model",
         "{\"type\":\"embedding\",\"dimensions\":3072,\"max_tokens\":8191}"},
        {NULL, NULL, NULL, NULL}};

    for (int i = 0; builtin[i][0] && a->model_count < OPENAI_MAX_MODELS; i++) {
        openai_model_t *m = &a->models[a->model_count++];
        m->id = AIRY_STRDUP(builtin[i][0]);
        m->name = AIRY_STRDUP(builtin[i][1]);
        m->owned_by = AIRY_STRDUP("agentrt");
        m->is_default = (i == 0);
        m->is_available = true;
        m->max_context_tokens = 128000;
        m->max_output_tokens = 4096;
    }
}

int openai_list_models(openai_handle_t handle, const char *search_query, void *out_results)
{
    if (!handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_list_models: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    int count = 0;
    for (size_t i = 0; i < adapter->model_count; i++) {
        if (!search_query || strstr(adapter->models[i].name, search_query)) {
            count++;
        }
    }

    if (out_results) {
        openai_model_t *results =
            (openai_model_t *)AIRY_CALLOC(count > 0 ? (size_t)count : 1, sizeof(openai_model_t));
        if (results) {
            int idx = 0;
            for (size_t i = 0; i < adapter->model_count && idx < count; i++) {
                if (!search_query || strstr(adapter->models[i].name, search_query)) {
                    results[idx++] = adapter->models[i];
                }
            }
            *(openai_model_t **)out_results = results;
        }
    }

    return count;
}

void openai_free_model_list(void *list)
{
    if (!list)
        return;
    openai_model_t *models = (openai_model_t *)list;
    for (int i = 0; i < OPENAI_MAX_MODELS; i++) {
        if (models[i].name) {
            AIRY_FREE(models[i].name);
            models[i].name = NULL;
        }
        if (models[i].owned_by) {
            AIRY_FREE(models[i].owned_by);
            models[i].owned_by = NULL;
        }
    }
    AIRY_FREE(list);
}
