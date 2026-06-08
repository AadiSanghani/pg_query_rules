#ifndef PG_QUERY_RULES_RULES_H
#define PG_QUERY_RULES_RULES_H

#include "postgres.h"
#include "regex/regex.h"

#define PGQR_MAX_MATCHES 10

typedef enum PgqrRuleAction
{
    PGQR_RULE_ACTION_REWRITE,
    PGQR_RULE_ACTION_BLOCK
} PgqrRuleAction;

typedef enum PgqrRuleApplyKind
{
    PGQR_RULE_APPLY_NO_MATCH,
    PGQR_RULE_APPLY_REWRITE,
    PGQR_RULE_APPLY_BLOCK
} PgqrRuleApplyKind;

typedef struct PgqrRuleApplyResult
{
    PgqrRuleApplyKind kind;
    char *rewritten_query;
    char *error_message;
} PgqrRuleApplyResult;

typedef struct PgqrRule {
    int32 rule_id;
    char *username;
    PgqrRuleAction action;
    char *match_pattern;
    char *replace_pattern;
    char *error_message;
    bool stop_processing;
    int64 hits;

    regex_t regex;
    bool regex_compiled;
} PgqrRule;

extern void pgqr_rule_init(PgqrRule *rule, MemoryContext memory_context, int32 rule_id, const char *username, PgqrRuleAction action, const char *match_pattern, const char *replace_pattern, const char *error_message, bool stop_processing, int64 hits);

extern void pgqr_rule_free(PgqrRule *rule);

extern PgqrRuleApplyResult pgqr_rule_apply(PgqrRule *rule, const char *query_string, const char *username);

#endif
