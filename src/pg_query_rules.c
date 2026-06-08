#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"
#include "utils/acl.h"
#include "utils/builtins.h"

#include "hook.h"
#include "cache.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pgqr_version);
PG_FUNCTION_INFO_V1(load_query_rules_to_runtime);
PG_FUNCTION_INFO_V1(pgqr_test);

void
_PG_init(void)
{
    pgqr_cache_init();
    pgqr_hook_init();
}

void
_PG_fini(void)
{
    pgqr_hook_shutdown();
    pgqr_cache_shutdown();
}

Datum
pgqr_version(PG_FUNCTION_ARGS)
{
    PG_RETURN_TEXT_P(cstring_to_text("0.1.0"));
}

Datum
load_query_rules_to_runtime(PG_FUNCTION_ARGS)
{
    int rule_count;
    char result[64];

    rule_count = pgqr_cache_load();

    snprintf(result, sizeof(result), "loaded %d rules", rule_count);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

Datum
pgqr_test(PG_FUNCTION_ARGS)
{
    text *input_text;
    char *query_string;
    char *current_username;
    PgqrCacheApplyResult apply_result;
    text *result;

    input_text = PG_GETARG_TEXT_PP(0);
    query_string = text_to_cstring(input_text);
    current_username = GetUserNameFromId(GetUserId(), false);

    apply_result = pgqr_cache_apply(query_string, current_username);

    if (apply_result.kind == PGQR_CACHE_APPLY_BLOCK)
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("%s", apply_result.error_message)));

    if (apply_result.kind == PGQR_CACHE_APPLY_REWRITE)
        result = cstring_to_text(apply_result.rewritten_query);
    else
        result = cstring_to_text(query_string);

    pfree(query_string);
    pfree(current_username);

    if (apply_result.rewritten_query != NULL)
        pfree(apply_result.rewritten_query);

    if (apply_result.error_message != NULL)
        pfree(apply_result.error_message);

    PG_RETURN_TEXT_P(result);
}
