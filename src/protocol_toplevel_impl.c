// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B

#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include "../../commons/utils/error/error.h"
#include "error.h"

int protocol_adapter_create(airy_protocol_type_t type, protocol_adapter_t *adapter)
{
    if (!adapter) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_adapter_create: failed");
        return AIRY_ERR_UNKNOWN;
    }
    AIRY_MEMSET(adapter, 0, sizeof(*adapter));
    adapter->type = type;
    adapter->init = NULL;
    return 0;
}

void protocol_adapter_destroy(protocol_adapter_t adapter)
{
    if (adapter.context) {
        AIRY_FREE(adapter.context);
    }
}

int protocol_adapter_send(protocol_adapter_t adapter, const airy_message_t *msg)
{
    if (!adapter.init) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_adapter_send: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (!msg) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_adapter_send: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (adapter.send) {
        return adapter.send(adapter.context, msg->data, msg->len);
    }
    return AIRY_ERR_NOT_FOUND;
}

int protocol_adapter_recv(protocol_adapter_t adapter, airy_message_t *msg, size_t max_len)
{
    if (!adapter.init) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_adapter_recv: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (!msg) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_adapter_recv: IO error");
        return AIRY_ERR_UNKNOWN;
    }
    if (adapter.receive) {
        void *data = NULL;
        size_t size = 0;
        int rc = adapter.receive(adapter.context, &data, &size, 0);
        if (rc == 0 && data && size > 0) {
            size_t copy_len = size < max_len ? size : max_len;
            msg->len = copy_len;
            AIRY_FREE(data);
        }
        return rc;
    }
    return AIRY_ERR_NOT_FOUND;
}

const char *protocol_type_name(airy_protocol_type_t type)
{
    switch (type) {
    case AIRY_PROTOCOL_JSON_RPC:
        return "JSON-RPC";
    case AIRY_PROTOCOL_MCP:
        return "MCP";
    case AIRY_PROTOCOL_A2A:
        return "A2A";
    default:
        return "Unknown";
    }
}
