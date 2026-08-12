// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_retry.c
 * @brief OpenAI enterprise adapter rate-limit and retry domain (window rotation/429 backoff/statistics query).
 */

#include "openai_enterprise_adapter_internal.h"

#include "airy_memory.h"
#include "types.h"
#include "../../../../commons/utils/error/include/error.h"
#include "error.h"

#include <time.h>

static void openai_rate_window_rotate(struct openai_enterprise_adapter_s *adapter)
{
    time_t now = time(NULL);
    if (now - adapter->rate_window_start >= OPENAI_RATE_LIMIT_WINDOW_SEC) {
        adapter->rate_window_start = now;
        adapter->rate_window_requests = 0;
        adapter->rate_window_tokens = 0;
        if (adapter->rate_429_count == 0) {
            adapter->rate_backoff_multiplier = 1.0;
            adapter->rate_backoff_until = 0;
        }
    }
}

openai_rate_result_t openai_check_rate_limit(struct openai_enterprise_adapter_s *adapter,
                                             uint32_t estimated_tokens)
{
    openai_rate_window_rotate(adapter);
    time_t now = time(NULL);

    if (adapter->rate_backoff_until > 0 && now < adapter->rate_backoff_until) {
        return OPENAI_RATE_BACKOFF;
    }

    if (adapter->rate_window_requests >= adapter->rate_limit_rpm) {
        return OPENAI_RATE_LIMITED_RPM;
    }

    if (adapter->rate_limit_tpm > 0 &&
        adapter->rate_window_tokens + estimated_tokens > adapter->rate_limit_tpm) {
        return OPENAI_RATE_LIMITED_TPM;
    }

    return OPENAI_RATE_OK;
}

__attribute__((unused)) void openai_record_request(
    struct openai_enterprise_adapter_s *adapter, uint32_t input_tokens, uint32_t output_tokens)
{
    adapter->rate_window_requests++;
    adapter->rate_window_tokens += input_tokens + output_tokens;
}

void openai_on_429(struct openai_enterprise_adapter_s *adapter)
{
    time_t now = time(NULL);
    adapter->rate_429_count++;
    adapter->rate_last_429_time = now;

    double new_multiplier = adapter->rate_backoff_multiplier * 2.0;
    if (new_multiplier > 32.0)
        new_multiplier = 32.0;
    adapter->rate_backoff_multiplier = new_multiplier;

    uint32_t delay_sec =
        (uint32_t)(OPENAI_RETRY_BASE_DELAY_MS / 1000 * adapter->rate_backoff_multiplier);
    if (delay_sec < 1)
        delay_sec = 1;
    if (delay_sec > 30)
        delay_sec = 30;
    adapter->rate_backoff_until = now + delay_sec;
}

static int __attribute__((unused)) openai_compute_retry_delay_ms(
    struct openai_enterprise_adapter_s *adapter, int attempt)
{
    uint32_t base_delay = (uint32_t)(OPENAI_RETRY_BASE_DELAY_MS * adapter->rate_backoff_multiplier);
    double exponential = base_delay * (1 << attempt);
    if (exponential > OPENAI_RETRY_MAX_DELAY_MS)
        exponential = OPENAI_RETRY_MAX_DELAY_MS;

    unsigned int jitter = (unsigned int)(attempt * OPENAI_RETRY_JITTER_MS);
    jitter = jitter % OPENAI_RETRY_JITTER_MS;
    return (int)(exponential + (double)jitter);
}

/* ============================================================================
 * Statistics & Cleanup
 * ============================================================================ */

int openai_get_stats(void *handle, openai_rate_limit_t *out_stats)
{
    if (!handle || !out_stats) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (!adapter->initialized) {
        airy_err_push_ex(AIRY_ERR_SYS_NOT_INIT, __FILE__, __LINE__, __func__,
                         "openai: not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    AIRY_MEMSET(out_stats, 0, sizeof(*out_stats));
    out_stats->current_rpm = (double)adapter->rate_window_requests;
    out_stats->rpm_limit = (double)adapter->rate_limit_rpm;
    out_stats->current_tpm = (double)adapter->rate_window_tokens;
    out_stats->tpm_limit = (double)adapter->rate_limit_tpm;

    return 0;
}

int openai_set_rate_limits(void *handle, uint32_t rpm, uint32_t tpm)
{
    if (!handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_set_rate_limits: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    if (rpm > 0)
        adapter->rate_limit_rpm = rpm;
    if (tpm > 0)
        adapter->rate_limit_tpm = tpm;
    return 0;
}

int openai_get_rate_status(void *handle, uint32_t *out_remaining_rpm, uint32_t *out_remaining_tpm,
                           uint32_t *out_429_count, double *out_backoff)
{
    if (!handle) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "openai_get_rate_status: failed");
        return AIRY_ERR_UNKNOWN;
    }
    struct openai_enterprise_adapter_s *adapter = (struct openai_enterprise_adapter_s *)handle;
    openai_rate_window_rotate(adapter);

    if (out_remaining_rpm) {
        *out_remaining_rpm = (adapter->rate_limit_rpm > adapter->rate_window_requests) ?
                                 (adapter->rate_limit_rpm - adapter->rate_window_requests) :
                                 0;
    }
    if (out_remaining_tpm && adapter->rate_limit_tpm > 0) {
        *out_remaining_tpm = (adapter->rate_window_tokens < adapter->rate_limit_tpm) ?
                                 (adapter->rate_limit_tpm - adapter->rate_window_tokens) :
                                 0;
    }
    if (out_429_count)
        *out_429_count = adapter->rate_429_count;
    if (out_backoff)
        *out_backoff = adapter->rate_backoff_multiplier;
    return 0;
}
