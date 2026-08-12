// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_embed.c
 * @brief OpenAI enterprise adapter vector embedding domain (n-gram hash features + L2 normalization).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/include/error.h"
#include "error.h"

#include <math.h>
#include <string.h>

#include "platform.h"

/* ============================================================================
 * Embeddings
 * ============================================================================ */

int openai_create_embedding(openai_handle_t handle, const openai_embedding_request_t *request,
                            openai_embedding_response_t *out_response)
{
    if (!handle || !request || !out_response) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_create_embedding: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    uint64_t ts_start_ms = airy_time_ms();

    AIRY_MEMSET(out_response, 0, sizeof(*out_response));

    AIRY_STRNCPY_TERM(out_response->model,
                      request->model ? request->model : "text-embedding-ada-002",
                      sizeof(out_response->model));

    int dims = OPENAI_EMBEDDING_DIM_DEFAULT;
    if (request->model) {
        if (strstr(request->model, "3-large"))
            dims = 3072;
        else if (strstr(request->model, "3-small"))
            dims = 1536;
    }

    out_response->embeddings = AIRY_CALLOC(dims, sizeof(double));
    if (!out_response->embeddings) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "openai: out of memory");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    out_response->embedding_dim = (size_t)dims;

    float *accum = AIRY_CALLOC(dims, sizeof(float));
    if (accum) {
#define OPENAI_NGRAM_SIZE 3
        const char *input = request->input_text ? request->input_text : "";
        size_t input_len = strlen(input);

        for (size_t i = 0; i + OPENAI_NGRAM_SIZE <= input_len; i++) {
            uint64_t ngram_hash = OPENAI_FNV_OFFSET;
            for (size_t g = 0; g < OPENAI_NGRAM_SIZE; g++) {
                unsigned char c = (unsigned char)input[i + g];
                if (c >= 'A' && c <= 'Z')
                    c += 32;
                ngram_hash ^= c;
                ngram_hash *= OPENAI_FNV_PRIME;
            }
            int dim = (int)(ngram_hash % (uint64_t)dims);
            float sign = ((ngram_hash >> 32) & 1) ? 1.0f : -1.0f;
            accum[dim] += sign * (1.0f / sqrtf((float)(i + 1)));
        }

        uint64_t full_hash = openai_fnv1a_hash(input);
        for (int pass = 0; pass < 4; pass++) {
            uint64_t base_hash = full_hash ^ ((uint64_t)pass * 0x9E3779B97F4A7C15ULL);
            for (int d = 0; d < dims; d++) {
                uint64_t dim_hash = base_hash ^ ((uint64_t)d * 0x5851F42D4C957F2DULL);
                double freq_factor =
                    sin((double)d * 0.618033988749895 + (double)(pass * 1.618033988749895));
                accum[d] +=
                    (float)(freq_factor *
                            ((double)((dim_hash >> (pass * 8)) & 0xFF) / 256.0 - 0.5) * 0.5);
            }
        }
#undef OPENAI_NGRAM_SIZE

        double l2_norm = 0.0;
        for (int i = 0; i < dims; i++)
            l2_norm += (double)accum[i] * (double)accum[i];
        l2_norm = sqrt(l2_norm);

        if (l2_norm > 1e-10) {
            for (int i = 0; i < dims; i++)
                out_response->embeddings[i] = (double)(accum[i] / (float)l2_norm);
        } else {
            out_response->embeddings[0] = 1.0;
            for (int i = 1; i < dims; i++)
                out_response->embeddings[i] = 0.0;
        }
        AIRY_FREE(accum);
        accum = NULL;
    } else {
        for (int i = 0; i < dims; i++)
            out_response->embeddings[i] = 0.0;
    }

    uint64_t ts_end_ms = airy_time_ms();
    double latency_ms = (double)(ts_end_ms - ts_start_ms);

    out_response->usage.prompt_tokens = (uint32_t)openai_estimate_tokens(request->input_text);
    out_response->usage.total_tokens = out_response->usage.prompt_tokens;

    adapter->stats_embeddings++;
    adapter->stats_total_input_tokens += out_response->usage.prompt_tokens;
    openai_record_latency(adapter, latency_ms);

    return 0;
}
