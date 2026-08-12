// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
/**
 * @file protocol_router.c
 * @brief Protocol routing and transformation engine implementation.
 *
 * Adaptive routing and transformation for MCP/A2A/OpenAI API and other protocols.
 */

#include "../include/protocol_router.h"

#include "airy_memory.h"
#include "platform.h"
#include "protocol_transformers.h"
#include "safe_string_utils.h"
#include "types.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "error.h"

#define INDEX_NOT_FOUND (-1)

#define ROUTER_MAX_PARAMS 16
#define ROUTER_PARAM_NAME_LEN 64
#define ROUTER_PARAM_VAL_LEN 256

typedef struct {
    char name[ROUTER_PARAM_NAME_LEN];
    char value[ROUTER_PARAM_VAL_LEN];
} route_param_t;

typedef struct {
    route_param_t params[ROUTER_MAX_PARAMS];
    int param_count;
} route_match_info_t;

// ============================================================================
// ============================================================================

typedef struct rule_node_s {
    protocol_rule_t rule;
    message_transformer_t transformer;
    struct rule_node_s *next;
} rule_node_t;

struct protocol_router_s {
    protocol_type_t default_protocol;
    rule_node_t *rules;
    size_t rule_count;
    route_decision_func_t decision_func;

    uint64_t total_messages_routed;
    uint64_t messages_routed_success;
    uint64_t messages_routed_failed;
    uint64_t total_conversion_time_ns;
};

// ============================================================================
// ============================================================================

static rule_node_t *create_rule_node(const protocol_rule_t *rule,
                                     message_transformer_t transformer);
static void destroy_rule_node(rule_node_t *node);
static int match_endpoint(const char *pattern, const char *endpoint);
static int default_decision_func(const unified_message_t *message, const protocol_rule_t *rules,
                                 size_t rule_count);

// ============================================================================
// ============================================================================

protocol_router_handle_t protocol_router_create(protocol_type_t default_protocol)
{
    struct protocol_router_s *router =
        (struct protocol_router_s *)AIRY_CALLOC(1, sizeof(struct protocol_router_s));
    if (!router) {
        return NULL;
    }

    router->default_protocol = default_protocol;
    router->rules = NULL;
    router->rule_count = 0;
    router->decision_func = default_decision_func;

    router->total_messages_routed = 0;
    router->messages_routed_success = 0;
    router->messages_routed_failed = 0;
    router->total_conversion_time_ns = 0;

    return router;
}

void protocol_router_destroy(protocol_router_handle_t router)
{
    if (!router)
        return;

    struct protocol_router_s *r = (struct protocol_router_s *)router;

    rule_node_t *node = r->rules;
    while (node) {
        rule_node_t *next = node->next;
        destroy_rule_node(node);
        node = next;
    }

    AIRY_FREE(r);
}

int protocol_router_add_rule(protocol_router_handle_t router, const protocol_rule_t *rule,
                             message_transformer_t transformer)
{
    if (!router || !rule) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_router_add_rule: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_router_s *r = (struct protocol_router_s *)router;

    rule_node_t *node = create_rule_node(rule, transformer);
    if (!node) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "create_rule_node: failed");
        return AIRY_ERR_UNKNOWN;
    }

    rule_node_t **pp = &r->rules;
    while (*pp && (*pp)->rule.priority <= rule->priority) {
        pp = &(*pp)->next;
    }

    node->next = *pp;
    *pp = node;
    r->rule_count++;

    return 0;
}

int protocol_router_remove_rule(protocol_router_handle_t router,
                                const char *source_endpoint_pattern)
{
    if (!router || !source_endpoint_pattern) {
        airy_err_push_ex(AIRY_EINVAL, __FILE__, __LINE__, __func__,
                         "protocol_router_remove_rule: null parameter");
        return AIRY_EINVAL;
    }

    struct protocol_router_s *r = (struct protocol_router_s *)router;

    rule_node_t **pp = &r->rules;
    int removed = 0;
    while (*pp) {
        const protocol_rule_t *rule = &(*pp)->rule;
        int matched = 0;
        if (rule->source_endpoint) {
            matched = (strcmp(rule->source_endpoint, source_endpoint_pattern) == 0);
        } else {

            matched = (strcmp(source_endpoint_pattern, "*") == 0);
        }

        if (matched) {
            rule_node_t *victim = *pp;
            *pp = victim->next;
            destroy_rule_node(victim);
            r->rule_count--;
            removed++;
        } else {
            pp = &(*pp)->next;
        }
    }

    return removed > 0 ? 0 : AIRY_ERR_NOT_FOUND;
}

int protocol_router_route(protocol_router_handle_t router, const unified_message_t *message,
                          unified_message_t *transformed)
{
    if (!router || !message || !transformed) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_router_route: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_router_s *r = (struct protocol_router_s *)router;
    r->total_messages_routed++;

    uint64_t start_time = airy_time_ns();

    protocol_rule_t *rule_array = NULL;
    if (r->rule_count > 0) {
        SAFE_MALLOC_ARRAY(rule_array, r->rule_count, sizeof(protocol_rule_t));
        if (rule_array) {
            rule_node_t *node = r->rules;
            size_t i = 0;
            while (node && i < r->rule_count) {
                rule_array[i] = node->rule;
                node = node->next;
                i++;
            }
        }
    }

    int rule_index = -1;
    if (r->decision_func && rule_array) {
        rule_index = r->decision_func(message, rule_array, r->rule_count);
    }

    rule_node_t *matched_node = NULL;
    if (rule_index >= 0 && rule_index < (int)r->rule_count) {
        rule_node_t *node = r->rules;
        for (int i = 0; node && i < rule_index; i++) {
            node = node->next;
        }
        if (node) {
            matched_node = node;
        }
    }

    if (!matched_node) {
        *transformed = *message;
        transformed->protocol = r->default_protocol;

        {
            uint64_t end_time = airy_time_ns();
            if (start_time > 0) {
                r->total_conversion_time_ns += (end_time - start_time);
            }
        }

        r->messages_routed_success++;
        if (rule_array)
            AIRY_FREE(rule_array);
        return 0;
    }

    message_transformer_t transformer = matched_node->transformer;
    if (!transformer) {
        transformer = protocol_transformer_default;
    }

    int result = transformer(message, transformed, matched_node->rule.transformer_context);

    if (result == 0) {
        if (transformed->protocol == PROTOCOL_CUSTOM) {
            transformed->protocol = matched_node->rule.target_protocol;
        }
        if (!transformed->endpoint[0] && matched_node->rule.target_endpoint) {
            AIRY_STRNCPY_TERM(transformed->endpoint, matched_node->rule.target_endpoint,
                              sizeof(transformed->endpoint));
        }
    }

    uint64_t end_time = airy_time_ns();
    if (1) {
        if (start_time > 0) {
            r->total_conversion_time_ns += (end_time - start_time);
        }
    }

    if (result == 0) {
        r->messages_routed_success++;
    } else {
        r->messages_routed_failed++;
    }

    if (rule_array)
        AIRY_FREE(rule_array);
    return result;
}

int protocol_router_route_batch(protocol_router_handle_t router, const unified_message_t *messages,
                                size_t count, unified_message_t *transformed)
{
    if (!router || !messages || !transformed || count == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_router_route_batch: failed");
        return AIRY_ERR_UNKNOWN;
    }

    int success_count = 0;
    for (size_t i = 0; i < count; i++) {
        int result = protocol_router_route(router, &messages[i], &transformed[i]);
        if (result == 0) {
            success_count++;
        }
    }

    return success_count;
}

int protocol_router_set_decision_func(protocol_router_handle_t router,
                                      route_decision_func_t decision_func)
{
    if (!router) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_router_set_decision_func: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_router_s *r = (struct protocol_router_s *)router;
    r->decision_func = decision_func ? decision_func : default_decision_func;

    return 0;
}

int protocol_router_get_stats(protocol_router_handle_t router, char **stats_json)
{
    if (!router || !stats_json) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_router_get_stats: failed");
        return AIRY_ERR_UNKNOWN;
    }

    struct protocol_router_s *r = (struct protocol_router_s *)router;

    const char *fmt = "{"
                      "\"total_messages_routed\": %llu,"
                      "\"messages_routed_success\": %llu,"
                      "\"messages_routed_failed\": %llu,"
                      "\"average_conversion_time_ns\": %llu,"
                      "\"rule_count\": %zu"
                      "}";

    uint64_t avg_time = 0;
    if (r->total_messages_routed > 0) {
        avg_time = r->total_conversion_time_ns / r->total_messages_routed;
    }

    size_t buf_size = snprintf(NULL, 0, fmt, /* flawfinder: ignore - size-only probe, no write */
                               r->total_messages_routed, r->messages_routed_success,
                               r->messages_routed_failed, avg_time, r->rule_count) +
                      1;

    char *buf = (char *)AIRY_MALLOC(buf_size);
    if (!buf) {
        airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                         "AIRY_MALLOC: allocation failed");
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    snprintf(buf, buf_size, fmt, /* flawfinder: ignore - pre-sized buffer from prior probe */
             r->total_messages_routed, r->messages_routed_success, r->messages_routed_failed,
             avg_time, r->rule_count);

    *stats_json = buf;
    return 0;
}

// ============================================================================
// ============================================================================

int protocol_transformer_jsonrpc_to_mcp(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    return transformer_jsonrpc_to_mcp_request(source, target, context);
}

int protocol_transformer_mcp_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    return transformer_mcp_to_jsonrpc_response(source, target, context);
}

int protocol_transformer_openai_to_jsonrpc(const unified_message_t *source,
                                           unified_message_t *target, void *context)
{
    return transformer_openai_chat_to_jsonrpc(source, target, context);
}

int protocol_transformer_a2a_to_jsonrpc(const unified_message_t *source, unified_message_t *target,
                                        void *context)
{
    return transformer_a2a_to_jsonrpc_response(source, target, context);
}

int protocol_transformer_default(const unified_message_t *source, unified_message_t *target,
                                 void *context)
{
    if (!source || !target) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "protocol_transformer_default: failed");
        return AIRY_ERR_UNKNOWN;
    }

    *target = *source;

    if (source->payload && source->payload_size > 0) {
        void *new_payload = AIRY_MALLOC(source->payload_size);
        if (!new_payload) {

            target->payload = NULL;
            target->payload_size = 0;
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "protocol_transformer_default: payload allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        __builtin_memcpy(new_payload, source->payload, source->payload_size);
        target->payload = new_payload;
    }

    if (source->body && source->body_length > 0) {
        void *new_body = AIRY_MALLOC(source->body_length);
        if (!new_body) {
            /* Deep copy failed: target->body aliases *source via the shallow *target=*source;
              * set it NULL to avoid a double-free/dangling with source on the caller's free */
            target->body = NULL;
            target->body_length = 0;
            airy_err_push_ex(AIRY_ERR_OUT_OF_MEMORY, __FILE__, __LINE__, __func__,
                             "protocol_transformer_default: body allocation failed");
            return AIRY_ERR_OUT_OF_MEMORY;
        }
        __builtin_memcpy(new_body, source->body, source->body_length);
        target->body = new_body;
    }

    return 0;
}

// ============================================================================
// ============================================================================

static rule_node_t *create_rule_node(const protocol_rule_t *rule, message_transformer_t transformer)
{
    rule_node_t *node = (rule_node_t *)AIRY_CALLOC(1, sizeof(rule_node_t));
    if (!node) {
        return NULL;
    }

    node->rule = *rule;

    if (rule->source_endpoint) {
        size_t len = strlen(rule->source_endpoint) + 1;
        char *copy = (char *)AIRY_MALLOC(len);
        if (copy) {
            safe_strcpy(copy, rule->source_endpoint, len);
            node->rule.source_endpoint = copy;
        }
    }

    if (rule->target_endpoint) {
        size_t len = strlen(rule->target_endpoint) + 1;
        char *copy = (char *)AIRY_MALLOC(len);
        if (copy) {
            safe_strcpy(copy, rule->target_endpoint, len);
            node->rule.target_endpoint = copy;
        }
    }

    node->transformer = transformer;
    node->next = NULL;

    return node;
}

static void destroy_rule_node(rule_node_t *node)
{
    if (!node)
        return;

    if (node->rule.source_endpoint) {
        AIRY_FREE((void *)node->rule.source_endpoint);
    }

    if (node->rule.target_endpoint) {
        AIRY_FREE((void *)node->rule.target_endpoint);
    }

    if (node->rule.transformer_context) {
        AIRY_FREE(node->rule.transformer_context);
    }

    AIRY_FREE(node);
}

static int match_endpoint(const char *pattern, const char *endpoint)
{
    if (!pattern || !endpoint) {
        return 0;
    }

    size_t pat_len = strlen(pattern);
    size_t end_len = strlen(endpoint);

    if (pat_len == 0 || end_len == 0) {
        return (pat_len == 0 && end_len == 0);
    }

    if (strcmp(pattern, "*") == 0) {
        return 1;
    }

    if (strcmp(pattern, endpoint) == 0) {
        return 1;
    }

    if (pattern[0] == '*' && pattern[1] == '\0') {
        return 1;
    }

    if (pattern[pat_len - 1] == '/') {
        if (end_len >= pat_len && strncmp(pattern, endpoint, pat_len) == 0) {
            return 1;
        }
    }

    const char *p = pattern;
    const char *e = endpoint;

    while (*p && *e) {
        if (*p == '{') {
            const char *close = strchr(p, '}');
            if (!close)
                return 0;

            while (*e && *e != '/' && *e != '?' && *e != '#') {
                e++;
            }
            p = close + 1;
            continue;
        }

        if (*p == '*') {
            if (*(p + 1) == '*') {
                p += 2;
                while (*p == '/')
                    p++;
                if (!*p)
                    return 1;
                const char *next_slash = strchr(e, '/');
                if (next_slash) {
                    e = next_slash;
                } else {
                    e += strlen(e);
                }
                continue;
            } else {
                p++;
                while (*e && *e != '/' && *e != '?' && *e != '#') {
                    e++;
                }
                continue;
            }
        }

        if (*p == '?') {
            p++;
            e++;
            continue;
        }

        if (*p != *e) {
            return 0;
        }

        p++;
        e++;
    }

    if (!*p && !*e) {
        return 1;
    }

    while (*p == '/')
        p++;
    while (*e == '/')
        e++;

    if ((*p == '{' || *p == '*' || *p == '?') && !*e) {
        const char *tmp = p;
        int all_optional = 1;
        while (*tmp) {
            if (*tmp == '{') {
                const char *c = strchr(tmp, '}');
                if (c)
                    tmp = c + 1;
                else {
                    all_optional = 0;
                    break;
                }
            } else if (*tmp == '*') {
                tmp++;
            } else if (*tmp == '?') {
                tmp++;
            } else if (*tmp != '/') {
                all_optional = 0;
                break;
            } else {
                tmp++;
            }
        }
        if (all_optional)
            return 1;
    }

    return 0;
}

static int __attribute__((unused)) match_endpoint_extract(const char *pattern, const char *endpoint,
                                                          route_match_info_t *info)
{
    if (!pattern || !endpoint)
        return 0;
    if (info)
        __builtin_memset(info, 0, sizeof(*info));

    size_t pat_len = strlen(pattern);
    size_t end_len = strlen(endpoint);

    if (pat_len == 0 || end_len == 0) {
        return (pat_len == 0 && end_len == 0);
    }

    if (strcmp(pattern, "*") == 0)
        return 1;
    if (strcmp(pattern, endpoint) == 0)
        return 1;

    if (pattern[pat_len - 1] == '/') {
        if (end_len >= pat_len && strncmp(pattern, endpoint, pat_len) == 0)
            return 1;
    }

    const char *p = pattern;
    const char *e = endpoint;

    while (*p && *e) {
        if (*p == '{') {
            const char *close = strchr(p, '}');
            if (!close)
                return 0;
            if (info && info->param_count < ROUTER_MAX_PARAMS) {
                route_param_t *param = &info->params[info->param_count];
                size_t nlen = (size_t)(close - p - 1);
                if (nlen >= ROUTER_PARAM_NAME_LEN)
                    nlen = ROUTER_PARAM_NAME_LEN - 1;
                __builtin_memcpy(param->name, p + 1, nlen);
                param->name[nlen] = '\0';

                const char *val_start = e;
                while (*e && *e != '/' && *e != '?' && *e != '#')
                    e++;
                size_t vlen = (size_t)(e - val_start);
                if (vlen >= ROUTER_PARAM_VAL_LEN)
                    vlen = ROUTER_PARAM_VAL_LEN - 1;
                __builtin_memcpy(param->value, val_start, vlen);
                param->value[vlen] = '\0';
                info->param_count++;
            } else {
                while (*e && *e != '/' && *e != '?' && *e != '#')
                    e++;
            }
            p = close + 1;
            continue;
        }

        if (*p == '*') {
            if (*(p + 1) == '*') {
                p += 2;
                while (*p == '/')
                    p++;
                if (!*p)
                    return 1;
                if (info && info->param_count < ROUTER_MAX_PARAMS) {
                    route_param_t *param = &info->params[info->param_count];
                    AIRY_STRNCPY_TERM(param->name, "wildcard", ROUTER_PARAM_NAME_LEN);
                    const char *rest = e;
                    const char *next_seg = strchr(e, '/');
                    size_t vlen = next_seg ? (size_t)(next_seg - e) : strlen(e);
                    if (vlen >= ROUTER_PARAM_VAL_LEN)
                        vlen = ROUTER_PARAM_VAL_LEN - 1;
                    __builtin_memcpy(param->value, rest, vlen);
                    param->value[vlen] = '\0';
                    info->param_count++;
                }
                const char *next_slash = strchr(e, '/');
                if (next_slash)
                    e = next_slash;
                else
                    e += strlen(e);
                continue;
            } else {
                p++;
                if (info && info->param_count < ROUTER_MAX_PARAMS) {
                    route_param_t *param = &info->params[info->param_count];
                    AIRY_STRNCPY_TERM(param->name, "glob", ROUTER_PARAM_NAME_LEN);
                    const char *vs = e;
                    while (*e && *e != '/' && *e != '?' && *e != '#')
                        e++;
                    size_t vl = (size_t)(e - vs);
                    if (vl >= ROUTER_PARAM_VAL_LEN)
                        vl = ROUTER_PARAM_VAL_LEN - 1;
                    __builtin_memcpy(param->value, vs, vl);
                    param->value[vl] = '\0';
                    info->param_count++;
                } else {
                    while (*e && *e != '/' && *e != '?' && *e != '#')
                        e++;
                }
                continue;
            }
        }

        if (*p == '?') {
            p++;
            e++;
            continue;
        }
        if (*p != *e)
            return 0;
        p++;
        e++;
    }

    if (!*p && !*e)
        return 1;

    while (*p == '/')
        p++;
    while (*e == '/')
        e++;

    if ((*p == '{' || *p == '*') && !*e) {
        const char *tmp = p;
        int all_opt = 1;
        while (*tmp) {
            if (*tmp == '{') {
                const char *c = strchr(tmp, '}');
                if (c)
                    tmp = c + 1;
                else {
                    all_opt = 0;
                    break;
                }
            } else if (*tmp == '*' || *tmp == '?') {
                tmp++;
            } else if (*tmp != '/') {
                all_opt = 0;
                break;
            } else {
                tmp++;
            }
        }
        return all_opt;
    }

    return 0;
}

static int default_decision_func(const unified_message_t *message, const protocol_rule_t *rules,
                                 size_t rule_count)
{
    if (!message || !rules || rule_count == 0) {
        airy_err_push_ex(AIRY_ERR_UNKNOWN, __FILE__, __LINE__, __func__,
                         "default_decision_func: failed");
        return AIRY_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < rule_count; i++) {
        const protocol_rule_t *rule = &rules[i];

        if (rule->source_protocol != PROTOCOL_CUSTOM &&
            rule->source_protocol != message->protocol) {
            continue;
        }

        if (rule->source_endpoint && message->endpoint[0]) {
            if (!match_endpoint(rule->source_endpoint, message->endpoint)) {
                continue;
            }
        }

        return (int)i;
    }

    return INDEX_NOT_FOUND;
}