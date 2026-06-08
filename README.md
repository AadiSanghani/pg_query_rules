# pg_query_rules

`pg_query_rules` is a PostgreSQL C extension that rewrites or blocks SQL queries at runtime using regex-based rules.

It is inspired by ProxySQL query rules, but runs inside PostgreSQL as an extension instead of as a proxy layer.

## What It Does

Rules are stored in a `pg_query_rules` table. Active rules are loaded into an in-memory cache, and every incoming query is checked by a PostgreSQL parse hook.

A rule can:

- rewrite matching SQL into different SQL
- block matching SQL with a custom PostgreSQL error
- apply only to a specific database user
- stop further rule processing after it matches

The query hook only reads from memory. It does not query the rule table for every SQL statement.

## Quick Start

Start the development Postgres container:

```bash
docker compose up -d --build
docker compose exec pg_extension_dev bash
```

Inside the container, build and install the extension:

```bash
make clean
make
make install
```

Connect to the test database:

```bash
psql -U postgres -d testdb
```

Create the extension:

```sql
CREATE EXTENSION pg_query_rules;
```

Check that it loaded:

```sql
SELECT pgqr_version();
```

## Rewrite Example

Insert a rewrite rule:

```sql
INSERT INTO pg_query_rules (
    active,
    action,
    match_pattern,
    replace_pattern
)
VALUES (
    true,
    'rewrite',
    '^SELECT 1;?$',
    'SELECT 2'
);
```

Load rules into memory:

```sql
SELECT load_query_rules_to_runtime();
```

Test the rewrite without executing it:

```sql
SELECT pgqr_test('SELECT 1');
```

Run a real query:

```sql
SELECT 1;
```

Expected result:

```text
2
```

## Block Example

Insert a block rule:

```sql
INSERT INTO pg_query_rules (
    active,
    action,
    match_pattern,
    error_message
)
VALUES (
    true,
    'block',
    '^SELECT 99;?$',
    'SELECT 99 is blocked by pg_query_rules'
);
```

Reload runtime rules:

```sql
SELECT load_query_rules_to_runtime();
```

Run:

```sql
SELECT 99;
```

Expected result:

```text
ERROR:  SELECT 99 is blocked by pg_query_rules
```

## Rule Table

Important columns:

| Column | Purpose |
| --- | --- |
| `active` | Only active rules are loaded into runtime |
| `username` | Optional user filter; `NULL` means all users |
| `action` | Either `rewrite` or `block` |
| `match_pattern` | POSIX regex matched against incoming SQL text |
| `replace_pattern` | Replacement SQL for rewrite rules |
| `error_message` | Custom error for block rules |
| `stop_processing` | Stop evaluating later rules after this rule matches |
| `hits` | In-memory match counter initialized from the table |

After changing rules, call:

```sql
SELECT load_query_rules_to_runtime();
```

## How It Works

```text
Application sends SQL
        |
Postgres parses/analyzes query
        |
pg_query_rules hook runs
        |
rules are checked from memory
        |
matching rule rewrites or blocks query
        |
Postgres executes final query
```

Main source files:

- `src/rules.c`: single-rule regex matching and replacement logic
- `src/cache.c`: in-memory rule cache and rule loading via SPI
- `src/hook.c`: PostgreSQL parse hook and query replacement/blocking
- `src/pg_query_rules.c`: extension entry points and SQL-callable functions

## Notes

This is a prototype/testing extension.

Current limitations:

- rules must be manually loaded into runtime after table changes
- runtime cache is per backend process
- rewritten SQL must produce one statement
- rule storage is currently local to the same database

The intended future direction is to keep the hook memory-only while allowing rules to be loaded from other sources, such as a file, sidecar, or remote control plane.
