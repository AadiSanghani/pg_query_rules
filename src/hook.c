#include "postgres.h"

#include "miscadmin.h"
#include "nodes/pg_list.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "utils/acl.h"

#include "cache.h"
#include "hook.h"

static post_parse_analyze_hook_type previous_post_parse_analyze_hook = NULL;
static bool pgqr_in_rewrite = false;

static Query *
pgqr_parse_rewritten_query(const char *rewritten_query,
                           QueryEnvironment *query_env)
{
    List *raw_parsetrees;
    RawStmt *raw_stmt;
    Query *new_query;

    raw_parsetrees = raw_parser(rewritten_query, RAW_PARSE_DEFAULT);

    if (list_length(raw_parsetrees) != 1)
        ereport(ERROR,
                (errmsg("pg_query_rules rewrite must produce exactly one statement")));

    raw_stmt = linitial_node(RawStmt, raw_parsetrees);
    new_query = parse_analyze_fixedparams(raw_stmt,
                                          rewritten_query,
                                          NULL,
                                          0,
                                          query_env);

    return new_query;
}

static void
pgqr_post_parse_analyze_hook(ParseState *pstate,
                             Query *query,
                             JumbleState *jstate)
{
    const char *query_string;
    char *current_username;
    PgqrCacheApplyResult apply_result;
    Query *new_query;

    if (previous_post_parse_analyze_hook != NULL)
        previous_post_parse_analyze_hook(pstate, query, jstate);

    if (pgqr_in_rewrite)
        return;

    query_string = pstate->p_sourcetext;
    if (query_string == NULL)
        return;

    current_username = GetUserNameFromId(GetUserId(), false);
    apply_result = pgqr_cache_apply(query_string, current_username);
    pfree(current_username);

    if (apply_result.kind == PGQR_CACHE_APPLY_BLOCK)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("%s", apply_result.error_message)));
    }

    if (apply_result.kind == PGQR_CACHE_APPLY_REWRITE)
    {
        elog(LOG,
             "pg_query_rules: rewrite: \"%s\" -> \"%s\"",
             query_string,
             apply_result.rewritten_query);

        PG_TRY();
        {
            pgqr_in_rewrite = true;
            new_query = pgqr_parse_rewritten_query(apply_result.rewritten_query,
                                                   pstate->p_queryEnv);
            memcpy(query, new_query, sizeof(Query));
            pgqr_in_rewrite = false;
        }
        PG_CATCH();
        {
            pgqr_in_rewrite = false;
            pfree(apply_result.rewritten_query);
            PG_RE_THROW();
        }
        PG_END_TRY();

        pfree(apply_result.rewritten_query);
    }
}

void
pgqr_hook_init(void)
{
    previous_post_parse_analyze_hook = post_parse_analyze_hook;
    post_parse_analyze_hook = pgqr_post_parse_analyze_hook;
}

void
pgqr_hook_shutdown(void)
{
    post_parse_analyze_hook = previous_post_parse_analyze_hook;
    previous_post_parse_analyze_hook = NULL;
}
