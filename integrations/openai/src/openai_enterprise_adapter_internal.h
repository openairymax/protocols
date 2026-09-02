// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file openai_enterprise_adapter_internal.h
 * @brief Internal types and cross-file declarations shared by the OpenAI enterprise adapter split files.
 */

#ifndef OPENAI_ENTERPRISE_ADAPTER_INTERNAL_H
#define OPENAI_ENTERPRISE_ADAPTER_INTERNAL_H

#include <time.h>

#include "openai_enterprise_adapter.h"

typedef void *openai_handle_t;

typedef struct {
    char *model;
    openai_message_t *messages;
    size_t num_messages;
    float temperature;
    float top_p;
    int max_tokens;
    char *stop_sequences[4];
    const openai_tool_def_t *tools;
    size_t tool_count;
} openai_chat_request_t;

typedef struct {
    double *values;
    size_t dim;
    char *model;
} openai_embedding_data_t;

typedef struct {
    char *model;
    char *input_text;
    size_t embedding_dim;
} openai_embedding_request_t;

typedef struct {
    openai_embedding_data_t *data;
    size_t num_data;
} openai_embedding_list_t;

typedef void (*openai_stream_chunk_callback_t)(const char *chunk, size_t len, bool is_final,
                                               void *user_data);

typedef struct {
    char request_id[64];
    int index;
    struct {
        const char *content;
        const char *role;
    } delta;
    bool is_final;
} openai_stream_chunk_t;

#define OPENAI_VERSION "1.0"
#define OPENAI_DEFAULT_TIMEOUT_MS 60000
#define OPENAI_MAX_RESPONSE_LEN 4096
#define OPENAI_EMBEDDING_DIM_DEFAULT 1536
#define OPENAI_STREAM_CHUNK_SIZE 8
#define OPENAI_STATS_HISTORY_SIZE 128
#define OPENAI_FNV_PRIME 16777619ULL
#define OPENAI_FNV_OFFSET 2166136261ULL
#define OPENAI_RATE_LIMIT_RPM_DEFAULT 500
#define OPENAI_RATE_LIMIT_TPM_DEFAULT 150000
#define OPENAI_RATE_LIMIT_WINDOW_SEC 60
#define OPENAI_RETRY_MAX_ATTEMPTS 5
#define OPENAI_RETRY_BASE_DELAY_MS 1000
#define OPENAI_RETRY_MAX_DELAY_MS 30000
#define OPENAI_RETRY_JITTER_MS 200

struct openai_enterprise_adapter_s {
    openai_enterprise_config_t config;
    openai_model_t models[OPENAI_MAX_MODELS];
    size_t model_count;
    uint64_t request_counter;
    bool initialized;

    uint32_t stats_chat_completions;
    uint32_t stats_embeddings;
    uint32_t stats_streaming_sessions;
    uint64_t stats_total_input_tokens;
    uint64_t stats_total_output_tokens;
    double stats_total_latency_ms;
    double stats_min_latency_ms;
    double stats_max_latency_ms;
    double stats_latency_samples[OPENAI_STATS_HISTORY_SIZE];
    size_t stats_latency_index;
    size_t stats_latency_count;

    uint32_t rate_limit_rpm;
    uint32_t rate_limit_tpm;
    time_t rate_window_start;
    uint32_t rate_window_requests;
    uint32_t rate_window_tokens;
    uint32_t rate_429_count;
    time_t rate_last_429_time;
    double rate_backoff_multiplier;
    time_t rate_backoff_until;

    char *last_response_body;
    size_t last_response_len;
};

typedef enum {
    OPENAI_RATE_OK = 0,
    OPENAI_RATE_LIMITED_RPM = 1,
    OPENAI_RATE_LIMITED_TPM = 2,
    OPENAI_RATE_BACKOFF = 3
} openai_rate_result_t;

struct openai_enterprise_context_s {
    openai_handle_t handle;
    openai_enterprise_config_t config;
    openai_chat_handler_t chat_handler;
    void *chat_handler_user_data;
    openai_embedding_handler_t embedding_handler;
    void *embedding_handler_user_data;
    openai_audit_handler_t audit_handler;
    void *audit_handler_user_data;
};

/* Global instance shared across files (was static; now external linkage) **/
extern struct openai_enterprise_adapter_s *g_openai_instance;

/* Helpers shared across files (was static; now external linkage) **/
void oai_register_builtin(struct openai_enterprise_adapter_s *a);
uint64_t openai_fnv1a_hash(const char *str);
void json_escape_string(const char *src, char *dst, size_t dst_size);
int oai_estimate_tokens(const char *text);
void oai_record_latency(struct openai_enterprise_adapter_s *adapter, double latency_ms);
int openai_api_call(const char *api_key, const char *base_url, const char *endpoint,
                    const char *request_json, char *out_buf, size_t buf_len);
int oai_parse_chat_resp(const char *json_str, char *content_out, size_t content_len,
                               openai_usage_t *usage);
openai_rate_result_t oai_check_rate_limit(struct openai_enterprise_adapter_s *adapter,
                                             uint32_t estimated_tokens);
__attribute__((unused)) void openai_record_request(
    struct openai_enterprise_adapter_s *adapter, uint32_t input_tokens, uint32_t output_tokens);
void openai_on_429(struct openai_enterprise_adapter_s *adapter);

/* handle-style APIs (defined in the split .c files for internal cross-file calls) **/
int openai_create(openai_enterprise_config_t config, openai_handle_t *out_handle);
void openai_destroy(openai_handle_t handle);
int openai_list_models(openai_handle_t handle, const char *search_query, void *out_results);
int oai_chat_completion(openai_handle_t handle, const openai_chat_request_t *request,
                           openai_chat_response_t *out_response);
int oai_stream_chat(openai_handle_t handle, const openai_chat_request_t *request,
                                     openai_streaming_handler_t on_chunk, void *user_data,
                                     openai_chat_response_t *final_summary);
int oai_create_embedding(openai_handle_t handle, const openai_embedding_request_t *request,
                            openai_embedding_response_t *out_response);
int openai_get_stats(void *handle, openai_rate_limit_t *out_stats);
void oai_free_model_list(void *list);
void openai_free_embedding_response(openai_embedding_response_t *response);
int oai_set_rate_limits(void *handle, uint32_t rpm, uint32_t tpm);
int oai_get_rate_status(void *handle, uint32_t *out_remaining_rpm, uint32_t *out_remaining_tpm,
                           uint32_t *out_429_count, double *out_backoff);

/* Protocol adapter callbacks (was static; referenced by openai_enterprise_get_adapter()) **/
int oai_init_cb(void *context);
int oai_destroy_cb(void *context);
int oai_encode_cb(void *c, const void *m, void **o, size_t *s);
int oai_decode_cb(void *c, const void *d, size_t s, void *o);
int oai_connect_cb(void *c, const char *endpoint);
int oai_disconnect_cb(void *c);
int oai_is_connected_cb(void *c);
int oai_send_cb(void *c, const void *d, size_t s);
int oai_receive_cb(void *c, void **d, size_t *s, uint32_t t);
int oai_handle_request_cb(void *c, const void *r, void **rp);
int oai_get_version_cb(void *c, char *b, size_t s);
uint32_t oai_capabilities_cb(void *c);
int oai_get_stats_cb(void *c, char *b, size_t s);

#endif /* OPENAI_ENTERPRISE_ADAPTER_INTERNAL_H */
