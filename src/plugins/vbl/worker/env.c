#include "env.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { char name[256]; Value *val; } Binding;

static Binding g_env[MAX_BINDINGS];
static int     g_env_count;

void env_init(void) { g_env_count = 0; memset(g_env, 0, sizeof(g_env)); }

void env_shutdown(void) {
    for (int i = 0; i < g_env_count; i++) val_free(g_env[i].val);
    g_env_count = 0;
}

int env_bind(const char *name, Value *val) {
    for (int i = 0; i < g_env_count; i++) {
        if (strcmp(g_env[i].name, name) == 0) { val_free(g_env[i].val); g_env[i].val = val; return 0; }
    }
    if (g_env_count >= MAX_BINDINGS) { fprintf(stderr, "ERROR: too many bindings\n"); return -1; }
    strncpy(g_env[g_env_count].name, name, 255);
    g_env[g_env_count].val = val;
    g_env_count++;
    return 0;
}

Value *env_lookup(const char *name) {
    for (int i = 0; i < g_env_count; i++)
        if (strcmp(g_env[i].name, name) == 0) return g_env[i].val;
    return NULL;
}
