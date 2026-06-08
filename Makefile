MODULE_big = pg_query_rules

PG_CONFIG = pg_config

OBJS = src/pg_query_rules.o \
       src/hook.o \
       src/cache.o \
       src/rules.o

EXTENSION = pg_query_rules

DATA = pg_query_rules--0.1.0.sql

PG_CPPFLAGS = -Wall -Wextra -Wno-unused-parameter
override with_llvm = no

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
