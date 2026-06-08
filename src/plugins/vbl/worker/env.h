#ifndef VBL_ENV_H
#define VBL_ENV_H

#include "types.h"

#define MAX_BINDINGS 256

void env_init(void);
void env_shutdown(void);
int env_bind(const char *name, Value *val);
Value *env_lookup(const char *name);
int  env_count(void);
void env_get(int i, const char **name, Value **val);

#endif
