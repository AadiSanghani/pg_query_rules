\echo Use "CREATE EXTENSION pg_query_rules" to load this file. \quit

-- Create the pg_query_rules table to store the rules. This is for prototyping purposes, in production this should be stored somewhere else.
CREATE TABLE pg_query_rules (
    rule_id         SERIAL          PRIMARY KEY,
    active          BOOLEAN         NOT NULL DEFAULT false,
    username        TEXT,
    action          TEXT            NOT NULL DEFAULT 'rewrite'
                                     CHECK (action IN ('rewrite', 'block')),
    match_pattern   TEXT            NOT NULL,
    replace_pattern TEXT,
    error_message   TEXT,
    stop_processing BOOLEAN         NOT NULL DEFAULT false,
    hits            BIGINT          NOT NULL DEFAULT 0,
    description     TEXT,
    updated_at      TIMESTAMPTZ     DEFAULT now()
);

COMMENT ON TABLE pg_query_rules IS
    'Regex-based query rewrite rules. Load into runtime with load_query_rules_to_runtime().';

COMMENT ON COLUMN pg_query_rules.match_pattern IS
    'POSIX regex to match against incoming query text';

COMMENT ON COLUMN pg_query_rules.action IS
    'Action to take when the rule matches: rewrite or block';

COMMENT ON COLUMN pg_query_rules.replace_pattern IS
    'Replacement string. Use $1, $2 etc for capture groups';

COMMENT ON COLUMN pg_query_rules.error_message IS
    'Custom error message used when action = block';

COMMENT ON COLUMN pg_query_rules.stop_processing IS
    'If true, stop evaluating further rules after this one matches';

COMMENT ON COLUMN pg_query_rules.hits IS
    'Number of times this rule has matched since last reload';

-- Trigger to update the updated_at column on each update.
CREATE OR REPLACE FUNCTION pg_query_rules_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER pg_query_rules_updated_at
    BEFORE UPDATE ON pg_query_rules
    FOR EACH ROW
    EXECUTE FUNCTION pg_query_rules_updated_at();

-- Function to return the version of the pg_query_rules extension.
CREATE FUNCTION pgqr_version()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgqr_version'
LANGUAGE C STRICT;

-- Function to load the rules into the runtime.
CREATE FUNCTION load_query_rules_to_runtime()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'load_query_rules_to_runtime'
LANGUAGE C STRICT;

-- Function to test rewrite rules without executing the rewritten query.
CREATE FUNCTION pgqr_test(sql_text TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgqr_test'
LANGUAGE C STRICT;
