// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file china_eco_crypto.h
 * @brief Internal header: SM3/SM4 cryptographic primitives for china_eco.
 *
 * NOT part of the public API — included only by china_eco_adapter.c
 * and china_eco_crypto.c.
 */

#ifndef CHINA_ECO_CRYPTO_INTERNAL_H
#define CHINA_ECO_CRYPTO_INTERNAL_H

#include "china_eco_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SM3 and SM4 public API — implementations live in china_eco_crypto.c.
 * Signatures match the public API declared in china_eco_adapter.h. */

#ifdef __cplusplus
}
#endif

#endif /* CHINA_ECO_CRYPTO_INTERNAL_H */
