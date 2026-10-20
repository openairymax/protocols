/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/* @owner: team-B */
/**
 * @file protocol_detect.h
 * @brief Payload-level protocol detection.
 *
 * Single JSON signature probe shared by every gateway front end: classifies
 * a raw request body as JSON-RPC / MCP / A2A / LLM chat-completions.
 * Transport-level hints (HTTP Content-Type, URL path) stay with the caller;
 * this probe reads the payload bytes only. Unknown payloads resolve to
 * AIRY_PROTOCOL_COUNT, the established "unknown, auto-detect" sentinel.
 */

#ifndef AIRY_RT_PROTOCOL_DETECT_H
#define AIRY_RT_PROTOCOL_DETECT_H

#include "unified_protocol.h"

#include "airy_abi.h"

#include <stddef.h>

AIRY_ABI_BEGIN

/* ============================================================================ */

/**
 * @brief Classify a JSON request body by protocol signature
 * @param data Raw request body bytes (NUL-terminated not required)
 * @param size Body size in bytes
 * @return Canonical protocol type; AIRY_PROTOCOL_COUNT when unknown
 *
 * Judgment order follows the wire facts: a JSON-RPC 2.0 envelope first, then
 * MCP/A2A method namespaces inside that envelope ("tools/call", "tasks/send",
 * ...), then the LLM API chat shape, then A2A structural and keyword
 * fallbacks.
 */
protocol_type_t proto_detect_body(const char *data, size_t size);

/* ============================================================================ */

AIRY_ABI_END

#endif /* AIRY_RT_PROTOCOL_DETECT_H */
