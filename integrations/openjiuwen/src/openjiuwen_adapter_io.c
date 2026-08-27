// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter_io.c
 * @brief OpenJiuwen 协议适配器-连接/收发 ops 域：adapter 层 connect/
 *        disconnect/send/receive 四个 ops 实现（底层 TCP 传输见
 *        openjiuwen_adapter_net.c，消息格式转换见 openjiuwen_adapter_msg.c）。
 *        自 openjiuwen_adapter.c 按功能域拆分，无外部 API 变化。
 */

#include "openjiuwen_adapter.h"
#include "openjiuwen_adapter_internal.h"

#include "airy_memory.h"
#include "error.h"
#include "logging.h"
#include "network_common.h"
#include "safe_string_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int openjiuwen_adapter_connect(void *context, const char *endpoint)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_ERR_NULL_POINTER;
    if (endpoint && endpoint[0])
        safe_strcpy(adapter->config.endpoint, endpoint, sizeof(adapter->config.endpoint));

    adapter->conn_state = OPENJIUWEN_CONN_CONNECTING;

    int rc = openjiuwen_net_connect(adapter);
    if (rc == 0)
        return 0;

    adapter->conn_state = OPENJIUWEN_CONN_DISCONNECTED;
    AIRY_LOG_WARN("OpenJiuwen: connection to %s failed (rc=%d)", adapter->config.endpoint, rc);
    return rc;
}

int openjiuwen_adapter_disconnect(void *context)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;
    if (!adapter)
        return AIRY_EINVAL;

    openjiuwen_net_disconnect(adapter);
    adapter->last_heartbeat_sec = 0;
    AIRY_LOG_INFO("OpenJiuwen: disconnected");
    return 0;
}

int openjiuwen_send_message(void *context, const void *data, size_t size)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter || !data) {
        return AIRY_ERR_NULL_POINTER;
    }

    if (!adapter->initialized) {
        AIRY_LOG_ERROR("OpenJiuwen adapter not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    const unified_message_t *message = (const unified_message_t *)data;

    char buffer[OPENJIUWEN_MAX_MESSAGE_SIZE];
    int result = openjiuwen_unified_to_native(message, buffer, sizeof(buffer));
    if (result < 0) {
        AIRY_LOG_ERROR("Failed to convert message to OpenJiuwen format");
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-result);
        return AIRY_ERR_NULL_POINTER;
    }

    int send_result = openjiuwen_send_with_retry(adapter, buffer, result);
    if (send_result != 0) {
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-send_result);
        AIRY_LOG_ERROR("OpenJiuwen: send failed after retries (errors=%u)", adapter->consecutive_errors);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    uint32_t now = openjiuwen_get_timestamp();
    if (now - adapter->last_heartbeat_sec >= OPENJIUWEN_HEARTBEAT_INTERVAL_SEC) {
        adapter->last_heartbeat_sec = now;
    }

    AIRY_LOG_DEBUG("Message sent to OpenJiuwen (id=%u, size=%d bytes)", adapter->message_counter,
              result);

    return (int)size;
}

/**
  * @brief Receive a message from the OpenJiuwen platform
 */
int openjiuwen_receive_message(void *context, void **data, size_t *size, uint32_t timeout_ms)
{
    openjiuwen_adapter_t *adapter = (openjiuwen_adapter_t *)context;

    if (!adapter || !data) {
        return AIRY_ERR_NULL_POINTER;
    }

    if (!adapter->initialized) {
        AIRY_LOG_ERROR("OpenJiuwen adapter not initialized");
        return AIRY_ERR_SYS_NOT_INIT;
    }

    if (adapter->conn_state != OPENJIUWEN_CONN_CONNECTED || !adapter->connection_handle) {
        AIRY_LOG_WARN("OpenJiuwen: cannot receive - not connected (state=%d)", adapter->conn_state);
        return AIRY_ENOTCONN;
    }

    network_connection_t *conn = (network_connection_t *)adapter->connection_handle;
    network_set_timeout(conn, (int)(timeout_ms > 0 ? timeout_ms : OPENJIUWEN_TIMEOUT_MS));

    /* Read the fixed frame header first. */
    openjiuwen_header_t header;
    AIRY_MEMSET(&header, 0, sizeof(header));
    size_t received = 0;
    airy_err_t err = network_receive(conn, &header, sizeof(header), &received);
    if (err != AIRY_SUCCESS || received < sizeof(header)) {
        if (err == AIRY_ERR_TIMEOUT || err == AIRY_ERR_WOULD_BLOCK)
            return err;
        adapter->consecutive_errors++;
        adapter->last_error_code = (uint32_t)(-err);
        openjiuwen_net_disconnect(adapter);
        return err != AIRY_SUCCESS ? err : AIRY_ERR_IO;
    }

    /* Validate header fields before trusting payload_length. */
    if (header.message_id == 0 || header.payload_length > OPENJIUWEN_MAX_MESSAGE_SIZE) {
        AIRY_LOG_WARN("OpenJiuwen: invalid frame header (id=%u len=%u)", header.message_id,
                 header.payload_length);
        return AIRY_ERR_PARSE_ERROR;
    }

    uint8_t *frame = (uint8_t *)AIRY_MALLOC(sizeof(header) + header.payload_length);
    if (!frame)
        return AIRY_ERR_OUT_OF_MEMORY;
    __builtin_memcpy(frame, &header, sizeof(header));

    size_t payload_read = 0;
    if (header.payload_length > 0) {
        err = network_receive(conn, frame + sizeof(header), header.payload_length, &payload_read);
        if (err != AIRY_SUCCESS || payload_read != header.payload_length) {
            AIRY_FREE(frame);
            adapter->consecutive_errors++;
            openjiuwen_net_disconnect(adapter);
            return err != AIRY_SUCCESS ? err : AIRY_ERR_IO;
        }
    }

    unified_message_t *msg = (unified_message_t *)AIRY_CALLOC(1, sizeof(unified_message_t));
    if (!msg) {
        AIRY_FREE(frame);
        return AIRY_ERR_OUT_OF_MEMORY;
    }
    msg->protocol = AIRY_PROTOCOL_OPENJIUWEN;
    msg->message_id = header.message_id;
    msg->timestamp = header.timestamp;
    safe_strcpy(msg->source_agent, header.source_agent, sizeof(msg->source_agent));
    safe_strcpy(msg->target_agent, header.target_agent, sizeof(msg->target_agent));
    if (header.payload_length > 0) {
        msg->payload = AIRY_MALLOC(header.payload_length);
        if (msg->payload) {
            __builtin_memcpy(msg->payload, frame + sizeof(header), header.payload_length);
            msg->payload_size = header.payload_length;
        } else {
            AIRY_FREE(frame);
            AIRY_FREE(msg);
            return AIRY_ERR_OUT_OF_MEMORY;
        }
    }
    AIRY_FREE(frame);

    adapter->consecutive_errors = 0;
    adapter->last_activity_ms = openjiuwen_get_timestamp_ms();

    uint32_t now = openjiuwen_get_timestamp();
    if (now - adapter->last_heartbeat_sec >= OPENJIUWEN_HEARTBEAT_INTERVAL_SEC)
        adapter->last_heartbeat_sec = now;

    *data = msg;
    if (size)
        *size = sizeof(unified_message_t);
    return 0;
}
