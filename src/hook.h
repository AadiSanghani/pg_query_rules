#ifndef PG_QUERY_RULES_HOOK_H
#define PG_QUERY_RULES_HOOK_H

#include "postgres.h"

extern void pgqr_hook_init(void);
extern void pgqr_hook_shutdown(void);

#endif