/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file protocol_adapter_prelude.h
 * @brief 协议适配器家族 include 前导块唯一真相源（SSoT）。
 *
 * 各协议适配器（frameworks/、integrations/、standards/ 下的多文件拆分单元）
 * 的内头逐字重抄同一段「项目公共头 + 标准 C 头」前导块，构成全树 3-copy
 * 克隆簇。本文件收敛该公共面：各内头在自身模块公共头之后仅保留本单行引用。
 *
 * 契约：
 *   - 仅收纳跨适配器共有的 include 面，不做任何类型、宏或函数定义
 *   - 消费 TU 的符号可达性由本前导块承接，语义与逐字重抄等价
 *   - 专属头（airy_protocol_interface.h、logging.h、protocol_transformers.h
 *     等）一律留在消费方内头，不得并入本面以抬高家族扇入
 *
 * 依据：0.1.19 架构改进方案 §4.3（机制件收敛）、L4 归位消解；台账 §149。
 */

#ifndef AIRY_RT_PROTOCOLS_ADAPTER_PRELUDE_H
#define AIRY_RT_PROTOCOLS_ADAPTER_PRELUDE_H

#include "error.h"
#include "airy_memory.h"
#include "types.h"
#include "unified_protocol.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#endif /* AIRY_RT_PROTOCOLS_ADAPTER_PRELUDE_H */
