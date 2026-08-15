// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file a2a_v03_adapter_auth.c
 * @brief A2A v0.3 authentication and encryption domain (PROTO-002: API key / HMAC-SHA256 / token and session management).
 */

#include "a2a_v03_adapter_internal.h"

#include "airy_memory.h"
#include "platform.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.h"
#include "logging.h"

#define A2A_MAX_SESSIONS 128
#define A2A_MAX_TOKENS 256

uint64_t a2a_timestamp_ms(void)
{
    return airy_time_ms();
}

static void a2a_hex_encode(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len && (i * 2 + 2) < out_size; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/* ============================================================================
  * SHA-256 + HMAC-SHA256 (FIPS 180-4 / RFC 2104, self-contained C)
 *
  * PROTO-002 requires real HMAC-SHA256 for A2A signing; the old djb2 32-bit hash
  * had no key and no one-wayness, forgeable offline. Standard implementation below, no deps.
 * ============================================================================ */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} a2a_sha256_ctx_t;

static const uint32_t a2a_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t a2a_ror32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static void a2a_sha256_transform(a2a_sha256_ctx_t *ctx)
{
    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)ctx->data[i * 4] << 24) | ((uint32_t)ctx->data[i * 4 + 1] << 16) |
               ((uint32_t)ctx->data[i * 4 + 2] << 8) | (uint32_t)ctx->data[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = a2a_ror32(w[i - 15], 7) ^ a2a_ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = a2a_ror32(w[i - 2], 17) ^ a2a_ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (size_t i = 0; i < 64; i++) {
        uint32_t S1 = a2a_ror32(e, 6) ^ a2a_ror32(e, 11) ^ a2a_ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + a2a_sha256_k[i] + w[i];
        uint32_t S0 = a2a_ror32(a, 2) ^ a2a_ror32(a, 13) ^ a2a_ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void a2a_sha256_init(a2a_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->datalen = 0;
}

static void a2a_sha256_update(a2a_sha256_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = p[i];
        if (ctx->datalen == 64) {
            a2a_sha256_transform(ctx);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void a2a_sha256_final(a2a_sha256_ctx_t *ctx, uint8_t *out)
{
    uint64_t bitlen = ctx->bitlen + (uint64_t)ctx->datalen * 8;
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {
        while (ctx->datalen < 64)
            ctx->data[ctx->datalen++] = 0;
        a2a_sha256_transform(ctx);
        ctx->bitlen += 512;
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56)
        ctx->data[ctx->datalen++] = 0;
    for (int i = 0; i < 8; i++)
        ctx->data[56 + i] = (uint8_t)(bitlen >> (56 - i * 8));
    a2a_sha256_transform(ctx);

    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static void a2a_hmac_sha256(const void *key, size_t key_len, const void *msg, size_t msg_len,
                            uint8_t *out)
{
    uint8_t k[64];
    __builtin_memset(k, 0, sizeof(k));
    if (key_len > 64) {
        a2a_sha256_ctx_t c;
        a2a_sha256_init(&c);
        a2a_sha256_update(&c, key, key_len);
        a2a_sha256_final(&c, k);
    } else {
        __builtin_memcpy(k, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    a2a_sha256_ctx_t c;
    a2a_sha256_init(&c);
    a2a_sha256_update(&c, ipad, sizeof(ipad));
    a2a_sha256_update(&c, msg, msg_len);
    uint8_t inner[32];
    a2a_sha256_final(&c, inner);

    a2a_sha256_init(&c);
    a2a_sha256_update(&c, opad, sizeof(opad));
    a2a_sha256_update(&c, inner, sizeof(inner));
    a2a_sha256_final(&c, out);
}

static int a2a_const_time_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return (diff == 0) ? 1 : 0;
}

static uint32_t a2a_simple_hash(const char *data, size_t len)
{
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (uint8_t)data[i];
    }
    return hash;
}

typedef struct {
    bool initialized;
    a2a_auth_config_t config;
    a2a_auth_token_t tokens[A2A_MAX_TOKENS];
    size_t token_count;
    a2a_session_t sessions[A2A_MAX_SESSIONS];
    size_t session_count;
    int failed_attempts;
    uint64_t lockout_until;
} a2a_auth_state_t;

static a2a_auth_state_t g_a2a_auth = {0};

static void a2a_generate_token_string(char *token_buf, size_t buf_size, const char *agent_id,
                                      uint64_t timestamp)
{
    /* HMAC-SHA256-based pseudo-random token: token = hex(HMAC(secret, agent_id|timestamp)).
      * Unpredictable with a key; falls back to time-shuffling without one (insecure setups). */
    const char *src = agent_id ? agent_id : "anonymous";
    uint8_t raw[32];
    size_t src_len = strlen(src);
    if (src_len > 32)
        src_len = 32;

    if (g_a2a_auth.initialized && g_a2a_auth.config.secret_len > 0 &&
        g_a2a_auth.config.secret_len <= 64) {
        uint8_t msg[64];
        size_t off = 0;
        __builtin_memcpy(msg, src, src_len);
        off += src_len;
        for (int i = 0; i < 8; i++)
            msg[off++] = (uint8_t)(timestamp >> (i * 8));
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, msg, off,
                        raw);
    } else {
        for (size_t i = 0; i < sizeof(raw); i++) {
            raw[i] = (uint8_t)(a2a_simple_hash(src, src_len) ^ (timestamp >> (i % 8)) ^
                               (uint8_t)(i * 37 + 0xAB) ^ (uint8_t)((timestamp * (i + 1)) & 0xFF));
        }
    }

    a2a_hex_encode(raw, sizeof(raw), token_buf, buf_size);
    if (buf_size > 0)
        token_buf[buf_size - 1] = '\0';
}

int a2a_v03_auth_init(a2a_v03_context_t *ctx, const a2a_auth_config_t *auth_config)
{
    if (!ctx || !auth_config) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_auth_init: failed");
        return AIRY_ERR_UNKNOWN;
    }

    AIRY_MEMSET(&g_a2a_auth, 0, sizeof(g_a2a_auth));
    g_a2a_auth.initialized = true;
    g_a2a_auth.config = *auth_config;

    if (g_a2a_auth.config.max_failed_attempts <= 0)
        g_a2a_auth.config.max_failed_attempts = A2A_MAX_FAILED_AUTH_ATTEMPTS;
    if (g_a2a_auth.config.token_ttl_sec == 0)
        g_a2a_auth.config.token_ttl_sec = A2A_TOKEN_EXPIRY_SEC;
    if (g_a2a_auth.config.max_sessions == 0)
        g_a2a_auth.config.max_sessions = A2A_MAX_SESSIONS;

    return 0;
}

void a2a_v03_auth_shutdown(a2a_v03_context_t *ctx)
{
    if (!ctx)
        return;

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        AIRY_MEMSET(&g_a2a_auth.tokens[i], 0, sizeof(g_a2a_auth.tokens[i]));
    }
    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        AIRY_MEMSET(&g_a2a_auth.sessions[i], 0, sizeof(g_a2a_auth.sessions[i]));
    }

    AIRY_MEMSET(&g_a2a_auth.config.shared_secret, 0, sizeof(g_a2a_auth.config.shared_secret));
    AIRY_MEMSET(&g_a2a_auth, 0, sizeof(g_a2a_auth));
}

int a2a_v03_authenticate(a2a_v03_context_t *ctx, const char *agent_id, const char *credential,
                         a2a_auth_token_t **out_token)
{
    if (!ctx || !agent_id || !credential || !out_token) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_authenticate: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    uint64_t now = a2a_timestamp_ms() / 1000;

    if (g_a2a_auth.lockout_until > 0 && now < g_a2a_auth.lockout_until) {
        AIRY_LOG_ERROR("authentication locked out: agent_id=%s, lockout_until=%llu, now=%llu", agent_id,
                  (unsigned long long)g_a2a_auth.lockout_until, (unsigned long long)now);

        airy_err_push_ex(
            AIRY_ERR_PERMISSION_DENIED, __FILE__, __LINE__, __func__,
            "a2a_v03_authenticate: authentication locked out (too many failed attempts)");
        return AIRY_ERR_PERMISSION_DENIED;
    }

    int cred_valid = 0;
    switch (g_a2a_auth.config.method) {
    case A2A_AUTH_API_KEY:

        cred_valid = (strlen(credential) == (size_t)g_a2a_auth.config.secret_len) &&
                     a2a_const_time_eq((const uint8_t *)credential,
                                       (const uint8_t *)g_a2a_auth.config.shared_secret,
                                       (size_t)g_a2a_auth.config.secret_len);
        break;
    case A2A_AUTH_HMAC_SHA256: {
        /* Credential is hex(HMAC-SHA256(shared_secret, agent_id)):
          * compute the expected value with real HMAC and compare in constant time (djb2 was forgeable) */
        if (g_a2a_auth.config.secret_len <= 0 || g_a2a_auth.config.secret_len > 64) {
            cred_valid = 0;
            break;
        }
        uint8_t expect[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, agent_id,
                        strlen(agent_id), expect);
        char expect_hex[65];
        a2a_hex_encode(expect, sizeof(expect), expect_hex, sizeof(expect_hex));
        cred_valid =
            (strlen(credential) == 64) &&
            a2a_const_time_eq((const uint8_t *)expect_hex, (const uint8_t *)credential, 64);
        break;
    }
    case A2A_AUTH_NONE:
    default:
        cred_valid = 1;
        break;
    }

    if (!cred_valid) {
        AIRY_LOG_ERROR("authentication failed: agent_id=%s, method=%d, failed_attempts=%d", agent_id,
                  g_a2a_auth.config.method, g_a2a_auth.failed_attempts + 1);
        g_a2a_auth.failed_attempts++;
        if (g_a2a_auth.failed_attempts >= g_a2a_auth.config.max_failed_attempts) {
            g_a2a_auth.lockout_until = now + 300;
            g_a2a_auth.failed_attempts = 0;
        }
        airy_err_push_ex(AIRY_ERR_BUFFER_TOO_SMALL, __FILE__, __LINE__, __func__,
                         "operation failed");
        return AIRY_ERR_BUFFER_TOO_SMALL;
    }

    g_a2a_auth.failed_attempts = 0;

    if (g_a2a_auth.token_count >= A2A_MAX_TOKENS) {
        __builtin_memmove(&g_a2a_auth.tokens[0], &g_a2a_auth.tokens[1],
                          (A2A_MAX_TOKENS - 1) * sizeof(a2a_auth_token_t));
        g_a2a_auth.token_count--;
    }

    a2a_auth_token_t *tok = &g_a2a_auth.tokens[g_a2a_auth.token_count++];
    AIRY_MEMSET(tok, 0, sizeof(*tok));

    AIRY_STRNCPY_TERM(tok->agent_id, agent_id, sizeof(tok->agent_id));
    tok->issued_at = now;
    tok->expires_at = now + g_a2a_auth.config.token_ttl_sec;
    tok->permissions = 0xFFFFFFFF;
    tok->valid = true;

    a2a_generate_token_string(tok->token, sizeof(tok->token), agent_id, now);

    *out_token = tok;
    return 0;
}

int a2a_v03_verify_token(a2a_v03_context_t *ctx, const char *token_str,
                         a2a_auth_token_t **out_token)
{
    if (!ctx || !token_str || !out_token) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_verify_token: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    uint64_t now = a2a_timestamp_ms() / 1000;

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        a2a_auth_token_t *tok = &g_a2a_auth.tokens[i];
        if (!tok->valid)
            continue;
        if (strcmp(tok->token, token_str) != 0)
            continue;

        if (now >= tok->expires_at) {
            AIRY_LOG_WARN("token expired: agent_id=%s, expires_at=%llu, now=%llu", tok->agent_id,
                     (unsigned long long)tok->expires_at, (unsigned long long)now);
            tok->valid = false;
            airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
            return AIRY_ERR_UNKNOWN;
        }

        if (out_token)
            *out_token = tok;
        return 0;
    }

    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    AIRY_LOG_WARN("token not found or invalid: token_count=%zu", g_a2a_auth.token_count);
    return AIRY_ERR_UNKNOWN;
}

int a2a_v03_invalidate_token(a2a_v03_context_t *ctx, const char *token_str)
{
    if (!ctx || !token_str) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_invalidate_token: invalid parameter");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < g_a2a_auth.token_count; i++) {
        if (g_a2a_auth.tokens[i].valid && strcmp(g_a2a_auth.tokens[i].token, token_str) == 0) {
            AIRY_MEMSET(&g_a2a_auth.tokens[i], 0, sizeof(g_a2a_auth.tokens[i]));
            return 0;
        }
    }

    airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__,
                     "memset: error AIRY_ERR_NULL_POINTER");
    return AIRY_ERR_NULL_POINTER;
}

const char *a2a_v03_sign_request(a2a_v03_context_t *ctx, const char *method,
                                 const char *params_json, const char *token_str,
                                 char *out_signature, size_t sig_buf_size)
{
    if (!ctx || !method || !params_json || !out_signature || sig_buf_size < 65)
        return NULL;
    if (!g_a2a_auth.initialized)
        return NULL;

    /* Signed message: method|params|token.
      * Note: the old code embedded the current timestamp, so verification failed unless
      * it ran in the same millisecond; replay protection comes from the session/token TTL. */
    char sign_data[4096];
    int len = snprintf(sign_data, sizeof(sign_data), "%s|%s|%s", method, params_json,
                       token_str ? token_str : "");
    if (len <= 0 || len >= (int)sizeof(sign_data))
        return NULL;

    if (g_a2a_auth.config.method == A2A_AUTH_HMAC_SHA256 && g_a2a_auth.config.secret_len > 0 &&
        g_a2a_auth.config.secret_len <= 64) {
        uint8_t mac[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, sign_data,
                        (size_t)len, mac);
        a2a_hex_encode(mac, sizeof(mac), out_signature, sig_buf_size);
        return out_signature;
    }
    if (g_a2a_auth.config.secret_len > 0 && g_a2a_auth.config.secret_len <= 64) {
        uint8_t mac[32];
        a2a_hmac_sha256(g_a2a_auth.config.shared_secret, g_a2a_auth.config.secret_len, sign_data,
                        (size_t)len, mac);
        a2a_hex_encode(mac, sizeof(mac), out_signature, sig_buf_size);
        return out_signature;
    }
    uint32_t hash = a2a_simple_hash(sign_data, (size_t)len);
    snprintf(out_signature, sig_buf_size, "%08x%08x%08x%08x", hash, hash ^ 0xA5A5A5A5,
             hash ^ 0x5A5A5A5A, hash ^ 0x12345678);
    return out_signature;
}

int a2a_v03_verify_signature(a2a_v03_context_t *ctx, const char *method, const char *params_json,
                             const char *signature, const char *token_str)
{
    if (!ctx || !method || !params_json || !signature) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_verify_signature: failed");
        return AIRY_ERR_UNKNOWN;
    }

    char expected[65];
    if (!a2a_v03_sign_request(ctx, method, params_json, token_str, expected, sizeof(expected))) {
        airy_err_push_ex(AIRY_ERR_NULL_POINTER, __FILE__, __LINE__, __func__, "operation failed");
        return AIRY_ERR_NULL_POINTER;
    }

    if (strlen(signature) >= 64 &&
        a2a_const_time_eq((const uint8_t *)expected, (const uint8_t *)signature, 64) == 1)
        return 0;
    AIRY_LOG_ERROR("signature verification failed: method=%s, expected vs actual mismatch", method);
    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}

int a2a_v03_create_session(a2a_v03_context_t *ctx, const char *remote_agent_id,
                           a2a_auth_method_t auth_method, a2a_crypto_method_t crypto_method,
                           a2a_session_t **out_session)
{
    if (!ctx || !remote_agent_id || !out_session) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_create_session: failed");
        return AIRY_ERR_UNKNOWN;
    }
    if (!g_a2a_auth.initialized) {
        airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__, "not initialized");
        return AIRY_ERR_STATE_ERROR;
    }

    if (g_a2a_auth.session_count >= g_a2a_auth.config.max_sessions) {
        size_t oldest_idx = 0;
        uint64_t oldest_time = UINT64_MAX;
        for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
            if (g_a2a_auth.sessions[i].last_activity < oldest_time) {
                oldest_time = g_a2a_auth.sessions[i].last_activity;
                oldest_idx = i;
            }
        }
        AIRY_MEMSET(&g_a2a_auth.sessions[oldest_idx], 0, sizeof(a2a_session_t));
        g_a2a_auth.sessions[oldest_idx] = g_a2a_auth.sessions[g_a2a_auth.session_count - 1];
        g_a2a_auth.session_count--;
    }

    uint64_t now = a2a_timestamp_ms();

    a2a_session_t *sess = &g_a2a_auth.sessions[g_a2a_auth.session_count++];
    AIRY_MEMSET(sess, 0, sizeof(*sess));

    snprintf(sess->session_id, sizeof(sess->session_id), "sess_%s_%" PRIu64 "_%08x",
             remote_agent_id, (uint64_t)(now / 1000),
             a2a_simple_hash(remote_agent_id, strlen(remote_agent_id)));

    AIRY_STRNCPY_TERM(sess->remote_agent_id, remote_agent_id, sizeof(sess->remote_agent_id));
    sess->auth_method = auth_method;
    sess->crypto_method = crypto_method;
    sess->created_at = now;
    sess->last_activity = now;
    sess->authenticated = (auth_method != A2A_AUTH_NONE);
    sess->encrypted = (crypto_method != A2A_CRYPTO_NONE);

    *out_session = sess;
    return 0;
}

int a2a_v03_validate_session(a2a_v03_context_t *ctx, const char *session_id,
                             a2a_session_t **out_session)
{
    if (!ctx || !session_id || !out_session) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "a2a_v03_validate_session: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        a2a_session_t *sess = &g_a2a_auth.sessions[i];

        if (strncmp(sess->session_id, session_id, sizeof(sess->session_id)) != 0)
            continue;

        uint64_t now = a2a_timestamp_ms();
        uint64_t age_sec = (now - sess->created_at) / 1000;

        if (age_sec > (uint64_t)g_a2a_auth.config.token_ttl_sec * 2) {
            AIRY_LOG_WARN("session expired: session_id=%s, age_sec=%llu, ttl=%d", sess->session_id,
                     (unsigned long long)age_sec, g_a2a_auth.config.token_ttl_sec * 2);
            AIRY_MEMSET(sess, 0, sizeof(*sess));

            airy_err_push_ex(AIRY_ERR_STATE_ERROR, __FILE__, __LINE__, __func__,
                             "a2a_v03_validate_session: session expired");
            return AIRY_ERR_STATE_ERROR;
        }

        sess->last_activity = now;
        sess->request_count++;

        if (out_session)
            *out_session = sess;
        return 0;
    }

    airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__, "operation failed");
    return AIRY_ERR_UNKNOWN;
}

void a2a_v03_destroy_session(a2a_v03_context_t *ctx, const char *session_id)
{
    if (!ctx || !session_id)
        return;

    for (size_t i = 0; i < g_a2a_auth.session_count; i++) {
        if (strncmp(g_a2a_auth.sessions[i].session_id, session_id,
                    sizeof(g_a2a_auth.sessions[i].session_id)) == 0) {
            AIRY_MEMSET(&g_a2a_auth.sessions[i], 0, sizeof(a2a_session_t));
            return;
        }
    }
}

size_t a2a_v03_get_active_session_count(a2a_v03_context_t *ctx)
{
    (void)ctx;
    return g_a2a_auth.session_count;
}

const char *a2a_auth_method_string(a2a_auth_method_t method)
{
    switch (method) {
    case A2A_AUTH_NONE:
        return "none";
    case A2A_AUTH_API_KEY:
        return "api_key";
    case A2A_AUTH_HMAC_SHA256:
        return "hmac-sha256";
    case A2A_AUTH_JWT_BEARER:
        return "jwt-bearer";
    default:
        return "unknown";
    }
}

const char *a2a_crypto_method_string(a2a_crypto_method_t method)
{
    switch (method) {
    case A2A_CRYPTO_NONE:
        return "none";
    case A2A_CRYPTO_AES_128_GCM:
        return "aes-128-gcm";
    case A2A_CRYPTO_AES_256_GCM:
        return "aes-256-gcm";
    default:
        return "unknown";
    }
}
