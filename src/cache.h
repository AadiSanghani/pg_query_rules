#ifndef PG_QUERY_RULES_CACHE_H
#define PG_QUERY_RULES_CACHE_H

#include "postgres.h"

typedef enum PgqrCacheApplyKind
{
    PGQR_CACHE_APPLY_NO_MATCH,
    PGQR_CACHE_APPLY_REWRITE,
    PGQR_CACHE_APPLY_BLOCK
} PgqrCacheApplyKind;

typedef struct PgqrCacheApplyResult
{
    PgqrCacheApplyKind kind;
    char *rewritten_query;
    char *error_message;
} PgqrCacheApplyResult;

extern void pgqr_cache_init(void);
extern void pgqr_cache_shutdown(void);

extern int pgqr_cache_load(void);

extern PgqrCacheApplyResult pgqr_cache_apply(const char *query_string,
                                             const char *current_username);

extern int pgqr_cache_rule_count(void);
extern bool pgqr_cache_is_loaded(void);

#endif
