/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file proto_http.h
 * @brief 协议家族共享 HTTP(S) JSON POST 机制件（curl 仪式单点化）。
 *
 * 三个厂商集成适配器此前各自重抄同一套 curl 写回调与 POST 仪式（增重缓冲、
 * setopt 序列、perform/getinfo/清理），构成本家族最大跨文件克隆簇。本机制件
 * 收敛该公共面：URL 拼装、鉴权头构造、响应解释与错误码映射一律留在消费方
 * （策略层），本件只承载传输机制。
 *
 * 契约：
 *   - 仅协议家族内部使用，不属于对外稳定 API
 *   - @p hdrs 为 NULL 结尾的 "Name: value" 字符串数组，机制件内部
 *     组装 slist 并统一释放
 *   - 返回 PROTO_HTTP_OK 时 *@p out_body 为 malloc 的 NUL 结尾响应体
 *     （调用方 AIRY_FREE），空响应体时为 NULL；*@p out_code 为 HTTP
 *     状态码
 *   - 返回 E_INIT / E_TRANSPORT 时不产生任何返回分配，且 *@p out_body
 *     置 NULL、*@p out_code 置 0
 *   - 未启用 curl 的构建配置下恒返回 PROTO_HTTP_E_INIT（无传输能力
 *     是真实契约，非桩实现；消费方各自门控 curl 可用性）
 *
 * 依据：0.1.19 架构改进方案 §4.3（机制件收敛）；台账 §176。
 */

#ifndef AIRY_RT_PROTOCOLS_PROTO_HTTP_H
#define AIRY_RT_PROTOCOLS_PROTO_HTTP_H

#include <stddef.h>

typedef enum {
    PROTO_HTTP_OK = 0,
    PROTO_HTTP_E_INIT,
    PROTO_HTTP_E_TRANSPORT
} proto_http_result_t;

proto_http_result_t proto_http_post(const char *url, const char *const *hdrs,
                                    const char *body, char **out_body, long *out_code);

#endif /* AIRY_RT_PROTOCOLS_PROTO_HTTP_H */
