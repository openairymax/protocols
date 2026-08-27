// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file china_eco_llm.h
 * @brief Internal header: LLM provider bridge for china_eco adapter.
 *
 * NOT part of the public API — included only by china_eco_adapter.c
 * and china_eco_llm.c.
 */

#ifndef CHINA_ECO_LLM_INTERNAL_H
#define CHINA_ECO_LLM_INTERNAL_H

#include "china_eco_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Provider API URL table (indexed by china_eco_provider_type_t). */
extern const char *g_provider_api_urls[];

/** Provider name table (indexed by china_eco_provider_type_t). */
extern const char *g_provider_names[];

/**
 * @brief OpenAI-compatible chat completion over HTTP(S).
 *
 * @return Length of extracted assistant content (>= 0) on success,
 *         or a negative airy error code on failure.
 */
int china_eco_llm_chat_http(const china_eco_llm_provider_t *provider,
                             const char *api_base_url, const char *model_id,
                             const char *messages_json, char *response, size_t resp_size,
                             uint64_t *prompt_tokens, uint64_t *completion_tokens);

#ifdef __cplusplus
}
#endif

#endif /* CHINA_ECO_LLM_INTERNAL_H */
