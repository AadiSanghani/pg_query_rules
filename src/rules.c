#include "postgres.h"

#include "catalog/pg_collation_d.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "utils/memutils.h"

#include "rules.h"

static int pgqr_char_to_byte_offset(const char *str, int char_offset)
{
    int byte_offset = 0;
    int chars_seen = 0;

    while (str[byte_offset] != '\0' && chars_seen < char_offset) {
        byte_offset += pg_mblen(str + byte_offset);
        chars_seen++;
    }

    return byte_offset;
}

static void pgqr_append_query_range(StringInfo buf, const char *query_string, int start_char, int end_char){
    int start_byte = pgqr_char_to_byte_offset(query_string, start_char);
    int end_byte = pgqr_char_to_byte_offset(query_string, end_char);
    
    if (end_byte < start_byte) {
        return;
    }

    appendBinaryStringInfo(buf, query_string + start_byte, end_byte - start_byte);
}

static void pgqr_append_replacement(StringInfo buf, const char *query_string, const char *replace_pattern, regmatch_t *matches)
{
    const char *ptr = replace_pattern;

    while (*ptr != '\0')
    {
        if (*ptr == '$' && ptr[1] >= '0' && ptr[1] <= '9') {
            int match_index = ptr[1] - '0';

            if (match_index < PGQR_MAX_MATCHES &&
                matches[match_index].rm_so >= 0 &&
                matches[match_index].rm_eo >= 0) {
                pgqr_append_query_range(buf,
                                        query_string,
                                        matches[match_index].rm_so,
                                        matches[match_index].rm_eo);
            }
            ptr += 2;
        } else if (*ptr == '$' && ptr[1] == '$') {
            appendStringInfoChar(buf, '$');
            ptr += 2;
        } else {
            appendStringInfoChar(buf, *ptr);
            ptr++;
        }
    }
}

void
pgqr_rule_init(PgqrRule *rule,
               MemoryContext memory_context,
               int32 rule_id,
               const char *username,
               PgqrRuleAction action,
               const char *match_pattern,
               const char *replace_pattern,
               const char *error_message,
               bool stop_processing,
               int64 hits)
{
    MemoryContext old_context = MemoryContextSwitchTo(memory_context);
    pg_wchar *wide_pattern; 
    int pattern_len;
    int wide_pattern_len;
    int regex_result;
    char regex_error[256];

    memset(rule, 0, sizeof(PgqrRule));

    if (action == PGQR_RULE_ACTION_REWRITE && replace_pattern == NULL)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("pg_query_rules.replace_pattern cannot be null for rewrite rule %d",
                        rule_id)));

    rule->rule_id = rule_id;
    rule->username = username == NULL ? NULL : pstrdup(username);
    rule->action = action;
    rule->match_pattern = pstrdup(match_pattern);
    rule->replace_pattern = replace_pattern == NULL ? NULL : pstrdup(replace_pattern);
    rule->error_message = error_message == NULL ? NULL : pstrdup(error_message);
    rule->stop_processing = stop_processing;
    rule->hits = hits;

    pattern_len = strlen(rule->match_pattern);
    wide_pattern = palloc(sizeof(pg_wchar) * (pattern_len + 1));
    wide_pattern_len = pg_mb2wchar_with_len(rule->match_pattern, wide_pattern, pattern_len);
    regex_result = pg_regcomp(&rule->regex,
        wide_pattern,
        wide_pattern_len,
        REG_ADVANCED,
        DEFAULT_COLLATION_OID);

    pfree(wide_pattern);

    if (regex_result != REG_OKAY){
        pg_regerror(regex_result,
            &rule->regex,
            regex_error,
            sizeof(regex_error));

        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("could not compile pg_query_rules regex for rule %d: %s",
                rule_id,
                regex_error)));
    }

    rule->regex_compiled = true;

    MemoryContextSwitchTo(old_context);
}

void
pgqr_rule_free(PgqrRule *rule)
{
    if (rule == NULL)
        return;

    if (rule->regex_compiled)
    {
        pg_regfree(&rule->regex);
        rule->regex_compiled = false;
    }
}

PgqrRuleApplyResult
pgqr_rule_apply(PgqrRule *rule,
                const char *query_string,
                const char *current_username)
{
    pg_wchar *wide_query;
    int query_len;
    int wide_query_len;
    int search_start;
    int last_append_char;
    int regex_result;
    bool matched;
    regmatch_t matches[PGQR_MAX_MATCHES];
    StringInfoData rewritten;
    PgqrRuleApplyResult result;

    result.kind = PGQR_RULE_APPLY_NO_MATCH;
    result.rewritten_query = NULL;
    result.error_message = NULL;

    if (rule == NULL || query_string == NULL)
        return result;

    if (rule->username != NULL)
    {
        if (current_username == NULL)
            return result;

        if (strcmp(rule->username, current_username) != 0)
            return result;
    }

    query_len = strlen(query_string);
    wide_query = palloc(sizeof(pg_wchar) * (query_len + 1));
    wide_query_len = pg_mb2wchar_with_len(query_string,
                                          wide_query,
                                          query_len);

    if (rule->action == PGQR_RULE_ACTION_BLOCK)
    {
        memset(matches, 0, sizeof(matches));

        regex_result = pg_regexec(&rule->regex,
                                  wide_query,
                                  wide_query_len,
                                  0,
                                  NULL,
                                  PGQR_MAX_MATCHES,
                                  matches,
                                  0);

        pfree(wide_query);

        if (regex_result == REG_NOMATCH)
            return result;

        if (regex_result != REG_OKAY)
            ereport(ERROR,
                    (errmsg("regex execution failed for pg_query_rules rule %d",
                            rule->rule_id)));

        rule->hits++;
        result.kind = PGQR_RULE_APPLY_BLOCK;
        result.error_message = pstrdup(rule->error_message != NULL ?
                                       rule->error_message :
                                       "query blocked by pg_query_rules");

        return result;
    }

    initStringInfo(&rewritten);

    search_start = 0;
    last_append_char = 0;
    matched = false;

    while (search_start <= wide_query_len)
    {
        memset(matches, 0, sizeof(matches));

        regex_result = pg_regexec(&rule->regex,
                                  wide_query,
                                  wide_query_len,
                                  search_start,
                                  NULL,
                                  PGQR_MAX_MATCHES,
                                  matches,
                                  0);

        if (regex_result == REG_NOMATCH)
            break;

        if (regex_result != REG_OKAY)
        {
            pfree(wide_query);
            pfree(rewritten.data);

            ereport(ERROR,
                    (errmsg("regex execution failed for pg_query_rules rule %d",
                            rule->rule_id)));
        }

        matched = true;

        pgqr_append_query_range(&rewritten,
                                query_string,
                                last_append_char,
                                matches[0].rm_so);

        pgqr_append_replacement(&rewritten,
                                query_string,
                                rule->replace_pattern,
                                matches);

        if (matches[0].rm_so == matches[0].rm_eo)
        {
            if (matches[0].rm_eo >= wide_query_len)
            {
                last_append_char = matches[0].rm_eo;
                break;
            }

            pgqr_append_query_range(&rewritten,
                                    query_string,
                                    matches[0].rm_eo,
                                    matches[0].rm_eo + 1);

            search_start = matches[0].rm_eo + 1;
            last_append_char = search_start;
        }
        else
        {
            search_start = matches[0].rm_eo;
            last_append_char = matches[0].rm_eo;
        }
    }

    pfree(wide_query);

    if (!matched)
    {
        pfree(rewritten.data);
        return result;
    }

    pgqr_append_query_range(&rewritten,
                            query_string,
                            last_append_char,
                            wide_query_len);

    rule->hits++;

    result.kind = PGQR_RULE_APPLY_REWRITE;
    result.rewritten_query = rewritten.data;

    return result;
}
