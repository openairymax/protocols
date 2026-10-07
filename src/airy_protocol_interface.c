// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file airy_protocol_interface.c
 * @brief AgentRT protocol system unified interface implementation.
 *
 * Global protocol directory registration & discovery. The concrete built-in
 * protocol set is supplied by the assembly layer through the proto_catalog_defs()
 * port; this translation unit carries no vendor knowledge
 * (mechanism/strategy separation, see 0.1.19 architecture plan §4.7/§5.1).
 *
 * Moved from agentrt/interfaces/src/ to agentrt/protocols/src/
 * (2026-04-19 interfaces removal refactor).
 */

#include "airy_protocol_interface.h"

#include "airy_memory.h"
#include "error.h"
#include "protocol_catalog.h"
#include "protocol_registry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static proto_adapter_entry_t *g_adapter_registry = NULL;
static size_t g_adapter_count = 0;

/* Global Registration & Discovery API Implementation */

int proto_interface_register_builtins(void)
{
    static bool registered = false;
    if (registered)
        return 0;

    protocol_registry_t *registry = proto_registry_get();
    if (!registry)
        return AIRY_EINVAL;

    size_t def_count = 0;
    const proto_builtin_def_t *defs = proto_catalog_defs(&def_count);
    if (!defs || def_count == 0)
        return AIRY_EINVAL;

    int count = proto_registry_register_builtins(registry, defs, def_count);
    if (count > 0) {
        proto_registry_entry_t *entries = NULL;
        size_t total = proto_registry_list_active(registry, &entries);

        for (size_t i = 0; i < total; i++) {
            proto_adapter_entry_t *entry =
                (proto_adapter_entry_t *)AIRY_CALLOC(1, sizeof(proto_adapter_entry_t));
            if (!entry)
                continue;

            entry->name = AIRY_STRDUP(entries[i].name);
            entry->version = AIRY_STRDUP(entries[i].version);
            entry->description = AIRY_STRDUP(entries[i].description);
            entry->type = entries[i].type;
            entry->capabilities = entries[i].capabilities;
            entry->is_builtin = true;
            entry->vtable = NULL;
            entry->next = g_adapter_registry;
            g_adapter_registry = entry;
            g_adapter_count++;
        }
        if (entries)
            AIRY_FREE(entries);
    }

    registered = true;
    return count;
}

const proto_adapter_entry_t *proto_interface_find(const char *name)
{
    if (!name)
        return NULL;
    proto_adapter_entry_t *entry = g_adapter_registry;
    while (entry) {
        if (strcmp(entry->name, name) == 0)
            return entry;
        entry = entry->next;
    }
    return NULL;
}

int proto_interface_list_all(char **json_output)
{
    if (!json_output)
        return AIRY_EINVAL;

    size_t buf_size = 512 + g_adapter_count * 256;
    char *buf = AIRY_MALLOC(buf_size);
    if (!buf)
        return AIRY_ERR_OUT_OF_MEMORY;

    size_t offset = snprintf(buf, buf_size, "{\"adapters\":[");
    proto_adapter_entry_t *entry = g_adapter_registry;
    while (entry) {
        if (entry != g_adapter_registry)
            offset += snprintf(buf + offset, buf_size - offset, ",");
        offset += snprintf(buf + offset, buf_size - offset, "\"%s\"",
                           entry->name ? entry->name : "unknown");
        entry = entry->next;
    }
    offset += snprintf(buf + offset, buf_size - offset, "],\"count\":%zu}", g_adapter_count);

    *json_output = buf;
    return 0;
}

const char *proto_interface_type_name(protocol_type_t type)
{
    if (type >= AIRY_PROTOCOL_VENDOR_BASE) {
        protocol_registry_t *registry = proto_registry_get();
        proto_registry_entry_t *entry =
            registry ? proto_registry_find_by_type(registry, type) : NULL;
        if (entry && entry->name[0])
            return entry->name;
    }
    return protocol_type_name(type);
}

protocol_type_t proto_interface_parse_type(const char *name)
{
    if (!name)
        return PROTOCOL_CUSTOM;

    protocol_registry_t *registry = proto_registry_get();
    proto_registry_entry_t *entry = registry ? proto_registry_find(registry, name) : NULL;
    if (entry)
        return entry->type;

    return protocol_type_from_string(name);
}
