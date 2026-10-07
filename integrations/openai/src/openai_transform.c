// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openai_transform.c
 * @brief OpenAI 面消息转换器实现（厂商面）。
 *
 * 由 core/transformers 迁出：厂商转换逻辑归属厂商集成目录，经装配层端口
 * proto_catalog_transforms() 注入机制核（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#include "openai_transform.h"

#include "protocol_transform_inline.h"

#include "airy_memory.h"
#include "error.h"
#include "llm_service_types.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>

int oai_chat_req(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_CUSTOM, "openai/chat/completions", DIRECTION_REQUEST);
    envelope_meta(target, context);

    char openai_payload[16384] = {0};
    const char *model = "gpt-4o";
    float temperature = 0.7f;
    int max_tokens = 2048;
    char messages_part[12000] = {0};

    if (source->payload) {
        const char *model_key = strstr((const char *)source->payload, "\"model\"");
        if (model_key) {
            const char *val_start = strchr(model_key + 6, '"');
            if (val_start) {
                model = val_start + 1;
            }
        }
        const char *temp_key = strstr((const char *)source->payload, "\"temperature\"");
        if (temp_key) {
            temperature = (float)strtod(temp_key + 13, NULL);
        }
        const char *max_tok_key = strstr((const char *)source->payload, "\"max_tokens\"");
        if (max_tok_key) {
            max_tokens = (int)strtol(max_tok_key + 12, NULL, 10);
        }
        const char *msgs_key = strstr((const char *)source->payload, "\"messages\"");
        if (msgs_key) {
            const char *arr_start = strchr(msgs_key + 9, '[');
            if (arr_start) {
                const char *arr_end = strrchr(arr_start, ']');
                if (arr_end) {
                    size_t len = arr_end - arr_start + 1;
                    AIRY_MEMCPY_SAFE(messages_part, arr_start, len, sizeof(messages_part));
                    messages_part[len] = '\0';
                }
            }
        }
    }

    if (messages_part[0] == '\0') {
        snprintf(messages_part, sizeof(messages_part), "[{\"role\":\"user\",\"content\":\"%s\"}]",
                 source->payload ? (const char *)source->payload : "");
    }

    snprintf(openai_payload, sizeof(openai_payload),
             "{\"model\":\"%s\",\"messages\":%s,\"temperature\":%.1f,"
             "\"max_tokens\":%d,\"stream\":false}",
             model, messages_part, temperature, max_tokens);

    target->payload = AIRY_STRDUP(openai_payload);
    target->payload_size = strlen(openai_payload) + 1;

    return 0;
}

int oai_chat_resp(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc", DIRECTION_RESPONSE);
    envelope_meta(target, context);

    if (!source->payload) {
        target->payload = AIRY_STRDUP(
            "{\"content\":\"\",\"finish_reason\":\"" LLM_FINISH_STOP "\","
            "\"usage\":{\"prompt_tokens\":0,\"completion_tokens\":0,\"total_tokens\":0}}");
        target->payload_size = strlen((const char *)target->payload) + 1;
        return 0;
    }

    const char *content = "";
    const char *finish_reason = LLM_FINISH_STOP;
    char usage_str[512] = "{}";

    const char *choices_key = strstr((const char *)source->payload, "\"choices\"");
    if (choices_key) {
        const char *message_key = strstr(choices_key, "\"message\"");
        if (message_key) {
            const char *content_key = strstr(message_key, "\"content\"");
            if (content_key) {
                const char *val_start = strchr(content_key + 8, '"');
                if (val_start)
                    content = val_start + 1;
            }
        }
        const char *finish_key = strstr(choices_key, "\"finish_reason\"");
        if (finish_key) {
            const char *val_start = strchr(finish_key + 14, '"');
            if (val_start)
                finish_reason = val_start + 1;
        }
    }

    const char *usage_key = strstr((const char *)source->payload, "\"usage\"");
    if (usage_key) {
        const char *obj_start = strchr(usage_key, '{');
        if (obj_start) {
            const char *obj_end = strchr(obj_start, '}');
            if (obj_end) {
                size_t len = obj_end - obj_start + 1;
                AIRY_MEMCPY_SAFE(usage_str, obj_start, len, sizeof(usage_str));
                usage_str[len] = '\0';
            }
        }
    }

    char result_buf[16384];
    snprintf(result_buf, sizeof(result_buf),
             "{\"content\":\"%s\",\"finish_reason\":\"%s\",\"usage\":%s}", content, finish_reason,
             usage_str);

    target->payload = AIRY_STRDUP(result_buf);
    target->payload_size = strlen(result_buf) + 1;

    return 0;
}

int oai_stream_resp(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc/llm.stream.chunk", DIRECTION_NOTIFICATION);
    envelope_meta(target, context);
    payload_pass(target, source, "{\"delta\":\"\"}");

    return 0;
}

int oai_embed_req(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_CUSTOM, "openai/embeddings", DIRECTION_REQUEST);
    envelope_meta(target, context);

    const char *text = "";
    const char *model = "text-embedding-ada-002";

    if (source->payload) {
        const char *t = strstr((const char *)source->payload, "\"text\"");
        if (t) {
            const char *v = strchr(t + 5, '"');
            if (v)
                text = v + 1;
        }
        const char *m = strstr((const char *)source->payload, "\"model\"");
        if (m) {
            const char *v = strchr(m + 6, '"');
            if (v)
                model = v + 1;
        }
    }

    char payload_buf[8192];
    snprintf(payload_buf, sizeof(payload_buf), "{\"model\":\"%s\",\"input\":\"%s\"}", model, text);

    target->payload = AIRY_STRDUP(payload_buf);
    target->payload_size = strlen(payload_buf) + 1;

    return 0;
}
