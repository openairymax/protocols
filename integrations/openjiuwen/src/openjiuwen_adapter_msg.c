// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter_msg.c
 * @brief OpenJiuwen 协议适配器-消息转换域：unified_message 与
 *        OpenJiuwen 帧格式互转及 encode/decode 两个 ops 包装。
 *        自 openjiuwen_adapter.c 按功能域拆分，无外部 API 变化。
 */

#include "openjiuwen_adapter.h"
#include "openjiuwen_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"
#include "safe_string_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int openjiuwen_adapter_encode(void *context, const void *msg, void **out_data,
                              size_t *out_size)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !msg || !out_data || !out_size)
        return AIRY_ERR_NULL_POINTER;
    if (!adapter->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    const unified_message_t *message = (const unified_message_t *)msg;
    char buffer[OPENJIUWEN_MAX_MESSAGE_SIZE];
    int result = openjiuwen_unified_to_native(message, buffer, sizeof(buffer));
    if (result < 0)
        return AIRY_ERR_NULL_POINTER;

    void *encoded = AIRY_MALLOC((size_t)result);
    if (!encoded)
        return AIRY_ERR_OUT_OF_MEMORY;
    __builtin_memcpy(encoded, buffer, (size_t)result);
    *out_data = encoded;
    *out_size = (size_t)result;
    return 0;
}

int openjiuwen_adapter_decode(void *context, const void *data, size_t size, void *out_msg)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter || !data || !out_msg)
        return AIRY_ERR_NULL_POINTER;
    if (!adapter->initialized)
        return AIRY_ERR_SYS_NOT_INIT;

    unified_message_t *msg = (unified_message_t *)out_msg;
    int result = openjiuwen_native_to_unified(data, size, msg);
    if (result < 0)
        return AIRY_ERR_NULL_POINTER;
    return 0;
}

int openjiuwen_unified_to_native(const unified_message_t *msg, void *out_buffer, size_t buffer_size)
{
    if (!msg || !out_buffer || buffer_size < sizeof(openjiuwen_header_t)) {
        return AIRY_ERR_NULL_POINTER;
    }

    openjiuwen_header_t header;
    AIRY_MEMSET(&header, 0, sizeof(header));

    header.message_id = openjiuwen_generate_message_id();
    header.timestamp = openjiuwen_get_timestamp();
    header.message_type = OPENJIUWEN_MSG_TYPE_REQUEST;
    header.flags = 0x0001;

    safe_strcpy(header.source_agent, msg->source_agent, sizeof(header.source_agent));
    safe_strcpy(header.target_agent, "OpenJiuwen", sizeof(header.target_agent));

    size_t payload_length = 0;
    if (msg->payload && msg->payload_size > 0) {
        payload_length = msg->payload_size;
    }
    header.payload_length = (uint32_t)payload_length;

    size_t total_size = sizeof(openjiuwen_header_t) + payload_length;
    if (total_size > buffer_size) {
        AIRY_LOG_ERROR("Buffer too small for OpenJiuwen message");
        return AIRY_ERR_IO;
    }

    __builtin_memcpy(out_buffer, &header, sizeof(openjiuwen_header_t));

    if (payload_length > 0 && msg->payload) {
        __builtin_memcpy((char *)out_buffer + sizeof(openjiuwen_header_t), msg->payload,
                         payload_length);
    }

    return (int)total_size;
}

int openjiuwen_native_to_unified(const void *in_buffer, size_t buffer_size, unified_message_t *msg)
{
    if (!in_buffer || !msg || buffer_size < sizeof(openjiuwen_header_t)) {
        return AIRY_ERR_NULL_POINTER;
    }

    const openjiuwen_header_t *header = (const openjiuwen_header_t *)in_buffer;

    if (buffer_size < sizeof(openjiuwen_header_t) + header->payload_length) {
        AIRY_LOG_ERROR("Invalid OpenJiuwen message: incomplete data");
        return AIRY_ERR_INVALID_PARAM;
    }

    AIRY_MEMSET(msg, 0, sizeof(unified_message_t));

    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    msg->message_id = header->message_id;
    msg->timestamp = header->timestamp;

    safe_strcpy(msg->source_agent, header->source_agent, sizeof(msg->source_agent));
    safe_strcpy(msg->target_agent, header->target_agent, sizeof(msg->target_agent));

    if (header->payload_length > 0) {
        msg->payload_size = header->payload_length;
        msg->payload = AIRY_MALLOC(header->payload_length);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, (const char *)in_buffer + sizeof(openjiuwen_header_t),
                             header->payload_length);
        } else {
            msg->payload_size = 0;
            return AIRY_ERR_NULL_POINTER;
        }
    }

    return 0;
}
