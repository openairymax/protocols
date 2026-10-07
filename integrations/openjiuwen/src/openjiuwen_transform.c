// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_transform.c
 * @brief OpenJiuwen 二进制帧转换器实现（厂商面）。
 *
 * 由 core/transformers 迁出：厂商转换逻辑归属厂商集成目录，经装配层端口
 * proto_catalog_transforms() 注入机制核（见 0.1.19 架构方案 §4.7/§5.1）。
 */

#include "openjiuwen_transform.h"

#include "protocol_transform_inline.h"

#include "airy_memory.h"
#include "error.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#define OPENJIUWEN_MAGIC 0x4F4A574DUL /* "OJWM" */
#define OPENJIUWEN_VERSION 0x0001

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t msg_type;
    uint64_t request_id;
    uint32_t payload_length;
} openjiuwen_header_t;
#pragma pack(pop)

int ojw_req(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target)
        return AIRY_ERR_NULL_POINTER;

    envelope_init(target, PROTOCOL_CUSTOM, "openjiuwen", DIRECTION_REQUEST);
    envelope_meta(target, context);

    openjiuwen_header_t header;
    AIRY_MEMSET(&header, 0, sizeof(header));
    header.magic = OPENJIUWEN_MAGIC;
    header.version = OPENJIUWEN_VERSION;
    header.msg_type = (uint32_t)(source->direction == DIRECTION_REQUEST ? 1 : 2);
    header.request_id = source->message_id;

    size_t payload_len = source->payload ? strlen((const char *)source->payload) : 0;
    header.payload_length = (uint32_t)payload_len;

    size_t total_size = sizeof(openjiuwen_header_t) + payload_len + 4;
    unsigned char *binary_data = AIRY_CALLOC(1, total_size);
    if (!binary_data)
        return AIRY_ERR_OUT_OF_MEMORY;

    __builtin_memcpy(binary_data, &header, sizeof(openjiuwen_header_t));

    if (source->payload && payload_len > 0) {
        __builtin_memcpy(binary_data + sizeof(openjiuwen_header_t), source->payload, payload_len);
    }

    uint32_t crc = 0;
    for (size_t i = 0; i < total_size - 4; i++) {
        crc ^= binary_data[i];
        crc = (crc << 1) | (crc >> 31);
    }
    __builtin_memcpy(binary_data + total_size - 4, &crc, sizeof(crc));

    target->payload = binary_data;
    target->payload_size = total_size;

    return 0;
}

int ojw_resp(const unified_message_t *source, unified_message_t *target, void *context)
{
    if (!source || !target || !source->payload ||
        source->payload_size < sizeof(openjiuwen_header_t)) {
        return AIRY_ERR_NULL_POINTER;
    }

    envelope_init(target, PROTOCOL_HTTP, "jsonrpc", DIRECTION_RESPONSE);
    envelope_meta(target, context);

    const unsigned char *data = (const unsigned char *)source->payload;
    const openjiuwen_header_t *hdr = (const openjiuwen_header_t *)data;

    if (hdr->magic != OPENJIUWEN_MAGIC) {
        target->direction = DIRECTION_RESPONSE;
        target->payload = AIRY_STRDUP("{\"error\":\"Invalid magic\"}");
        target->payload_size = 22;
        return 0;
    }

    target->message_id = hdr->request_id;
    target->direction = (hdr->msg_type == 2) ? DIRECTION_RESPONSE : DIRECTION_REQUEST;

    if (hdr->payload_length > 0 &&
        source->payload_size >= sizeof(openjiuwen_header_t) + hdr->payload_length) {

        const char *payload_ptr = (const char *)(data + sizeof(openjiuwen_header_t));
        size_t payload_len = hdr->payload_length;

        char *escaped = AIRY_MALLOC(payload_len * 2 + 256);
        if (escaped) {
            size_t j = 0;
            j += snprintf(escaped + j, payload_len * 2 + 256 - j, "{\"raw\":\"");
            for (size_t i = 0; i < payload_len && j < payload_len * 2 + 200; i++) {
                unsigned char c = (unsigned char)payload_ptr[i];
                if (c >= 32 && c < 127 && c != '"' && c != '\\') {
                    escaped[j++] = c;
                } else {
                    j += snprintf(escaped + j, 6, "\\x%02X", c);
                }
            }
            j += snprintf(escaped + j, 8, "\"}");
            target->payload = escaped;
            target->payload_size = j;
        }
    } else {
        target->payload = AIRY_STRDUP("{}");
        target->payload_size = 3;
    }

    return 0;
}
