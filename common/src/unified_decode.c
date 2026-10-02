// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file unified_decode.c
 * @brief Unified message field decode shared by all protocol adapters.
 *
 * 机制唯一实现：从 JSON 文本提取 protocol/direction/timestamp/payload 四字段
 * 填入 unified_message_t。各协议适配器仅保留各自的 guard 语义与默认协议枚举。
 */

#include "unified_protocol.h"

#include "airy_memory.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.h"

int unified_decode(const void *data, size_t size, unified_message_t *message,
                   protocol_type_t default_protocol)
{
    if (!data || !message || size == 0) {
        airy_err_push_ex(AIRY_ERR_INVALID_PARAM, __FILE__, __LINE__, __func__,
                         "unified_decode: invalid param");
        return AIRY_ERR_INVALID_PARAM;
    }

    char *copy = (char *)AIRY_MALLOC(size + 1);
    if (!copy) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "unified_decode: oom");
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(copy, data, size);
    copy[size] = '\0';

    message->protocol = default_protocol;
    char *p = strstr(copy, "\"protocol\":");
    if (p)
        message->protocol = (airy_protocol_type_t)strtol(p + 11, NULL, 10);

    message->direction = DIRECTION_RESPONSE;
    p = strstr(copy, "\"direction\":");
    if (p)
        message->direction = (message_direction_t)strtol(p + 12, NULL, 10);

    message->timestamp = (uint64_t)time(NULL);
    p = strstr(copy, "\"timestamp\":");
    if (p)
        message->timestamp = (uint64_t)strtoull(p + 12, NULL, 10);

    p = strstr(copy, "\"payload\":\"");
    if (p) {
        p += 11;
        char *end = strchr(p, '"');
        size_t plen = end ? (size_t)(end - p) : strlen(p);
        message->payload = AIRY_MALLOC(plen + 1);
        if (message->payload) {
            __builtin_memcpy(message->payload, p, plen);
            ((char *)message->payload)[plen] = '\0';
            message->payload_size = plen;
        }
    } else {
        message->payload = AIRY_STRDUP("");
        message->payload_size = 0;
    }

    AIRY_FREE(copy);
    return 0;
}
