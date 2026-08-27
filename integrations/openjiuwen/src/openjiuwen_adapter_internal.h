// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file openjiuwen_adapter_internal.h
 * @brief OpenJiuwen 协议适配器拆分后的跨文件共享声明。
 *        openjiuwen_adapter.c 按功能域拆分为 门面/生命周期、
 *        网络传输、连接收发 ops、消息转换 四个域后，各域共用本头声明。
 */

#ifndef OPENJIUWEN_ADAPTER_INTERNAL_H
#define OPENJIUWEN_ADAPTER_INTERNAL_H

#include "openjiuwen_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 工具：消息 ID/时间戳（门面域实现） ============ */
uint32_t openjiuwen_generate_message_id(void);
uint32_t openjiuwen_get_timestamp(void);
uint64_t openjiuwen_get_timestamp_ms(void);

/* ============ 网络传输域 (openjiuwen_adapter_net.c) ============ */
int openjiuwen_net_connect(openjiuwen_adapter_t *adapter);
void openjiuwen_net_disconnect(openjiuwen_adapter_t *adapter);
int openjiuwen_send_with_retry(openjiuwen_adapter_t *adapter, const char *buffer, int buffer_len);

/* ============ 连接/收发 ops 域 (openjiuwen_adapter_io.c) ============ */
int openjiuwen_adapter_connect(void *context, const char *endpoint);
int openjiuwen_adapter_disconnect(void *context);
int openjiuwen_send_message(void *context, const void *data, size_t size);
int openjiuwen_receive_message(void *context, void **data, size_t *size, uint32_t timeout_ms);

/* ============ 消息转换域 (openjiuwen_adapter_msg.c) ============ */
int openjiuwen_adapter_encode(void *context, const void *msg, void **out_data, size_t *out_size);
int openjiuwen_adapter_decode(void *context, const void *data, size_t size, void *out_msg);

/* ============ 门面/生命周期域 (openjiuwen_adapter.c) ============ */
int openjiuwen_adapter_init(void *context);
int openjiuwen_adapter_deinit(void *context);
int openjiuwen_adapter_is_connected(void *context);
int openjiuwen_adapter_handle_request(void *context, const void *req, void **resp);
int openjiuwen_adapter_get_version(void *context, char *version_buf, size_t max_size);
uint32_t openjiuwen_adapter_capabilities(void *context);
int openjiuwen_adapter_get_stats(void *context, char *stats_json, size_t max_size);
int openjiuwen_destroy(void *context);

#ifdef __cplusplus
}
#endif

#endif /* OPENJIUWEN_ADAPTER_INTERNAL_H */
