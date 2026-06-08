#include "postgres.h"

#include "access/htup_details.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "cache.h"
#include "rules.h"

static MemoryContext pgqr_cache_context = NULL;
static PgqrRule *pgqr_rules = NULL;
static int pgqr_rule_count_value = 0;
static bool pgqr_cache_loaded = false;

static PgqrCacheApplyResult
pgqr_cache_no_match_result(void)
{
    PgqrCacheApplyResult result;

    result.kind = PGQR_CACHE_APPLY_NO_MATCH;
    result.rewritten_query = NULL;
    result.error_message = NULL;

    return result;
}

static PgqrRuleAction
pgqr_parse_rule_action(const char *action)
{
    if (strcmp(action, "rewrite") == 0)
        return PGQR_RULE_ACTION_REWRITE;

    if (strcmp(action, "block") == 0)
        return PGQR_RULE_ACTION_BLOCK;

    ereport(ERROR,
            (errmsg("invalid pg_query_rules action: %s", action)));
}

static void
pgqr_cache_free_rules(PgqrRule *rules, int rule_count)
{
    int i;

    if (rules == NULL)
        return;

    for (i = 0; i < rule_count; i++)
        pgqr_rule_free(&rules[i]);
}

static void
pgqr_cache_clear(PgqrRule *rules,
                 int rule_count,
                 MemoryContext cache_context)
{
    pgqr_cache_free_rules(rules, rule_count);

    if (cache_context != NULL)
        MemoryContextDelete(cache_context);
}

void
pgqr_cache_init(void)
{
    pgqr_cache_context = NULL;
    pgqr_rules = NULL;
    pgqr_rule_count_value = 0;
    pgqr_cache_loaded = false;
}

void
pgqr_cache_shutdown(void)
{
    pgqr_cache_clear(pgqr_rules,
                     pgqr_rule_count_value,
                     pgqr_cache_context);

    pgqr_cache_context = NULL;
    pgqr_rules = NULL;
    pgqr_rule_count_value = 0;
    pgqr_cache_loaded = false;
}

int
pgqr_cache_load(void)
{
    MemoryContext volatile new_context;
    PgqrRule *volatile new_rules;
    volatile int new_rule_count;
    volatile bool spi_connected;
    int spi_result;
    uint64 processed;
    MemoryContext old_context;
    TupleDesc tupdesc;
    int i;

    new_context = NULL;
    new_rules = NULL;
    new_rule_count = 0;
    spi_connected = false;

    PG_TRY();
    {
        new_context = AllocSetContextCreate(TopMemoryContext,
                                            "pg_query_rules cache",
                                            ALLOCSET_DEFAULT_SIZES);

        spi_result = SPI_connect();

        if (spi_result != SPI_OK_CONNECT)
            ereport(ERROR,
                    (errmsg("could not connect to SPI while loading pg_query_rules")));

        spi_connected = true;

        spi_result = SPI_execute(
            "SELECT rule_id, "
            "       username, "
            "       action, "
            "       match_pattern, "
            "       replace_pattern, "
            "       error_message, "
            "       stop_processing, "
            "       hits "
            "FROM pg_query_rules "
            "WHERE active "
            "ORDER BY rule_id",
            true,
            0);

        if (spi_result != SPI_OK_SELECT)
            ereport(ERROR,
                    (errmsg("could not query pg_query_rules")));

        processed = SPI_processed;

        if (processed > PG_INT32_MAX)
            ereport(ERROR,
                    (errmsg("too many pg_query_rules rows to load")));

        old_context = MemoryContextSwitchTo((MemoryContext) new_context);

        if (processed > 0)
            new_rules = palloc0(sizeof(PgqrRule) * processed);

        MemoryContextSwitchTo(old_context);

        tupdesc = SPI_tuptable->tupdesc;

        for (i = 0; i < (int) processed; i++)
        {
            HeapTuple tuple;
            Datum datum;
            bool isnull;
            int32 rule_id;
            char *username;
            char *action;
            char *match_pattern;
            char *replace_pattern;
            char *error_message;
            bool stop_processing;
            int64 hits;

            tuple = SPI_tuptable->vals[i];

            datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            if (isnull)
                ereport(ERROR,
                        (errmsg("pg_query_rules.rule_id cannot be null")));
            rule_id = DatumGetInt32(datum);

            datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            username = isnull ? NULL : TextDatumGetCString(datum);

            datum = SPI_getbinval(tuple, tupdesc, 3, &isnull);
            if (isnull)
                ereport(ERROR,
                        (errmsg("pg_query_rules.action cannot be null")));
            action = TextDatumGetCString(datum);

            datum = SPI_getbinval(tuple, tupdesc, 4, &isnull);
            if (isnull)
                ereport(ERROR,
                        (errmsg("pg_query_rules.match_pattern cannot be null")));
            match_pattern = TextDatumGetCString(datum);

            datum = SPI_getbinval(tuple, tupdesc, 5, &isnull);
            replace_pattern = isnull ? NULL : TextDatumGetCString(datum);

            datum = SPI_getbinval(tuple, tupdesc, 6, &isnull);
            error_message = isnull ? NULL : TextDatumGetCString(datum);

            datum = SPI_getbinval(tuple, tupdesc, 7, &isnull);
            if (isnull)
                ereport(ERROR,
                        (errmsg("pg_query_rules.stop_processing cannot be null")));
            stop_processing = DatumGetBool(datum);

            datum = SPI_getbinval(tuple, tupdesc, 8, &isnull);
            if (isnull)
                ereport(ERROR,
                        (errmsg("pg_query_rules.hits cannot be null")));
            hits = DatumGetInt64(datum);

            pgqr_rule_init(&((PgqrRule *) new_rules)[i],
                           (MemoryContext) new_context,
                           rule_id,
                           username,
                           pgqr_parse_rule_action(action),
                           match_pattern,
                           replace_pattern,
                           error_message,
                           stop_processing,
                           hits);

            new_rule_count++;

            if (username != NULL)
                pfree(username);

            pfree(action);
            pfree(match_pattern);

            if (replace_pattern != NULL)
                pfree(replace_pattern);

            if (error_message != NULL)
                pfree(error_message);
        }

        spi_result = SPI_finish();

        if (spi_result != SPI_OK_FINISH)
            ereport(ERROR,
                    (errmsg("could not finish SPI while loading pg_query_rules")));

        spi_connected = false;

        pgqr_cache_clear(pgqr_rules,
                         pgqr_rule_count_value,
                         pgqr_cache_context);

        pgqr_cache_context = (MemoryContext) new_context;
        pgqr_rules = (PgqrRule *) new_rules;
        pgqr_rule_count_value = new_rule_count;
        pgqr_cache_loaded = true;
    }
    PG_CATCH();
    {
        if (spi_connected)
            SPI_finish();

        pgqr_cache_clear((PgqrRule *) new_rules,
                         new_rule_count,
                         (MemoryContext) new_context);

        PG_RE_THROW();
    }
    PG_END_TRY();

    return pgqr_rule_count_value;
}

PgqrCacheApplyResult
pgqr_cache_apply(const char *query_string,
                 const char *current_username)
{
    char *current_query;
    PgqrRuleApplyResult rule_result;
    PgqrCacheApplyResult cache_result;
    bool changed;
    int i;

    cache_result = pgqr_cache_no_match_result();

    if (!pgqr_cache_loaded)
        return cache_result;

    if (pgqr_rule_count_value <= 0)
        return cache_result;

    if (query_string == NULL)
        return cache_result;

    current_query = pstrdup(query_string);
    changed = false;

    for (i = 0; i < pgqr_rule_count_value; i++)
    {
        rule_result = pgqr_rule_apply(&pgqr_rules[i],
                                      current_query,
                                      current_username);

        if (rule_result.kind == PGQR_RULE_APPLY_NO_MATCH)
            continue;

        if (rule_result.kind == PGQR_RULE_APPLY_BLOCK)
        {
            pfree(current_query);

            cache_result.kind = PGQR_CACHE_APPLY_BLOCK;
            cache_result.error_message = rule_result.error_message;

            return cache_result;
        }

        pfree(current_query);

        current_query = rule_result.rewritten_query;
        changed = true;

        if (pgqr_rules[i].stop_processing)
            break;
    }

    if (!changed)
    {
        pfree(current_query);
        return cache_result;
    }

    cache_result.kind = PGQR_CACHE_APPLY_REWRITE;
    cache_result.rewritten_query = current_query;

    return cache_result;
}

int
pgqr_cache_rule_count(void)
{
    return pgqr_rule_count_value;
}

bool
pgqr_cache_is_loaded(void)
{
    return pgqr_cache_loaded;
}
