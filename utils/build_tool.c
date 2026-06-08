/*
 *  build_tool — BareMRI plugin build validator + code generator
 *
 *  Usage:  ./build_tool <profile_file>
 *
 *  Reads the profile, discovers .plugin descriptors under src/plugins/,
 *  validates conflicts/dependencies/exclusive hooks, and emits:
 *    - Makefile snippet (to stdout)
 *    - Auto-generated plugins.c (to build/plugins.c)
 *  Exits 0 on success, 1 on validation failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PLUGINS      32
#define MAX_LINE         256
#define MAX_NAME         64
#define MAX_HOOKS        16
#define MAX_PARAMS       16
#define MAX_DEPS         16
#define MAX_CONFLICTS    16
#define MAX_PANELS_DESC  8
#define PLUGINS_DIR      "src/plugins"

/* ── Plugin descriptor (loaded from .plugin files) ── */
typedef struct {
    char name[MAX_NAME];
    char hooks[MAX_HOOKS][MAX_NAME];
    int  hook_count;
    int  exclusive_mask;           /* bitmask of exclusive hooks */
    char params[MAX_PARAMS][MAX_NAME];
    int  param_count;
    char depends[MAX_DEPS][MAX_NAME];
    int  depends_count;
    char conflicts[MAX_CONFLICTS][MAX_NAME];
    int  conflicts_count;
    char panels[MAX_PANELS_DESC][MAX_NAME];
    int  panels_count;
    int  loaded;                   /* 1 = descriptor was found and parsed */
} PluginDesc;

/* ── Profile entry ── */
typedef struct {
    char name[MAX_NAME];
    char params[MAX_PARAMS][MAX_NAME];
    char param_vals[MAX_PARAMS][MAX_NAME];
    int  param_count;
} ProfileEntry;

/* ── Global state ── */
static PluginDesc  g_descs[MAX_PLUGINS];
static int          g_desc_count;

static ProfileEntry g_entries[MAX_PLUGINS];
static int           g_entry_count;

static char g_profile_name[MAX_NAME];     /* "large", "tall", etc. */

/* ── Forward declarations ── */
static int parse_profile(const char *path);
static int discover_plugins(void);
static int validate(void);

/* Find a descriptor by name. Returns NULL if not found. */
static PluginDesc *find_desc(const char *name) {
    for (int i = 0; i < g_desc_count; i++)
        if (strcmp(g_descs[i].name, name) == 0)
            return &g_descs[i];
    return NULL;
}

/* Check if plugin 'name' is in the profile */
static int in_profile(const char *name) {
    for (int i = 0; i < g_entry_count; i++)
        if (strcmp(g_entries[i].name, name) == 0)
            return 1;
    return 0;
}

/* Depth-first search for circular dependencies.
 * Returns 1 if a cycle is detected, 0 otherwise. */
static int dfs_cycle(int idx, int *visited, int *in_stack) {
    if (in_stack[idx]) return 1; /* cycle detected */
    if (visited[idx]) return 0;
    visited[idx] = 1;
    in_stack[idx] = 1;
    PluginDesc *pd = &g_descs[idx];
    for (int j = 0; j < pd->depends_count; j++) {
        PluginDesc *dep = find_desc(pd->depends[j]);
        if (!dep) continue;
        int dep_idx = (int)(dep - g_descs);
        if (dfs_cycle(dep_idx, visited, in_stack)) {
            fprintf(stderr, "ERROR: circular dependency involving '%s' -> '%s'\n",
                    pd->name, dep->name);
            return 1;
        }
    }
    in_stack[idx] = 0;
    return 0;
}
static void emit_makefile(void);
static void emit_plugins_c(void);

/* ================================================================ */

/* Trim trailing whitespace (including \r) */
static char *trim(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

/* Trim leading whitespace */
static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

static int parse_profile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "ERROR: cannot open profile '%s'\n", path); return 1; }

    char line[MAX_LINE];
    int in_plugins = 0;
    ProfileEntry *cur = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        /* profile "name" */
        if (strncmp(line, "profile", 7) == 0) {
            char *q = strchr(line, '"');
            if (q) {
                char *endq = strchr(q + 1, '"');
                if (endq) {
                    *endq = '\0';
                    strncpy(g_profile_name, q + 1, MAX_NAME - 1);
                    g_profile_name[MAX_NAME - 1] = '\0';
                }
            }
            continue;
        }

        /* plugins: */
        if (strncmp(line, "plugins:", 8) == 0) {
            in_plugins = 1;
            continue;
        }

        if (!in_plugins) continue;

        /* Plugin name possibly with params */
        if (strchr(line, '}')) { cur = NULL; continue; }

        char *brace = strchr(line, '{');
        if (brace) {
            *brace = '\0';
            char *name = ltrim(line);
            trim(name);
            if (g_entry_count >= MAX_PLUGINS) {
                fprintf(stderr, "ERROR: too many plugins (max %d)\n", MAX_PLUGINS);
                fclose(f);
                return 1;
            }
            cur = &g_entries[g_entry_count++];
            strncpy(cur->name, name, MAX_NAME - 1);
            cur->name[MAX_NAME - 1] = '\0';
            continue;
        }

        /* param = value or param  value inside braces */
        if (cur) {
            char *eq = strchr(line, '=');
            char *key = NULL, *val = NULL;
            if (eq) {
                *eq = '\0';
                key = ltrim(line);
                val = ltrim(eq + 1);
            } else {
                /* whitespace-separated: "key  value" */
                char *stripped = ltrim(line);
                char *space = strchr(stripped, ' ');
                if (!space) space = strchr(stripped, '\t');
                if (space) {
                    *space = '\0';
                    key = stripped;
                    val = ltrim(space + 1);
                }
            }
            if (key && *key && val && *val) {
                trim(key); trim(val);
                if (cur->param_count >= MAX_PARAMS) {
                    fprintf(stderr, "ERROR: too many params for plugin '%s' (max %d)\n",
                            cur->name, MAX_PARAMS);
                    fclose(f);
                    return 1;
                }
                int pi = cur->param_count;
                strncpy(cur->params[pi], key, MAX_NAME - 1);
                cur->params[pi][MAX_NAME - 1] = '\0';
                strncpy(cur->param_vals[pi], val, MAX_NAME - 1);
                cur->param_vals[pi][MAX_NAME - 1] = '\0';
                cur->param_count++;
                continue;
            }
        }

        /* Plain plugin name (no braces) */
        if (g_entry_count >= MAX_PLUGINS) {
            fprintf(stderr, "ERROR: too many plugins (max %d)\n", MAX_PLUGINS);
            fclose(f);
            return 1;
        }
        cur = &g_entries[g_entry_count++];
        strncpy(cur->name, ltrim(line), MAX_NAME - 1);
        cur->name[MAX_NAME - 1] = '\0';
    }
    fclose(f);

    if (g_profile_name[0] == '\0') {
        fprintf(stderr, "ERROR: no profile name in profile file\n");
        return 1;
    }
    if (g_entry_count == 0) {
        fprintf(stderr, "WARNING: no plugins listed in profile\n");
    }
    return 0;
}

/* Parse a .plugin file into a PluginDesc */
static int parse_plugin_file(const char *dirpath, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s.plugin", dirpath, name, name);

    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "ERROR: cannot open '%s'\n", path); return 1; }

    PluginDesc *d = &g_descs[g_desc_count];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, MAX_NAME - 1);
    d->name[MAX_NAME - 1] = '\0';
    d->loaded = 1;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = ltrim(line);
        trim(key);

        char *val = ltrim(eq + 1);
        trim(val);

        if (strcmp(key, "name") == 0) {
            if (strcmp(val, name) != 0) {
                fprintf(stderr, "ERROR: %s: name mismatch ('%s' vs '%s')\n",
                        path, val, name);
                fclose(f);
                return 1;
            }
        } else if (strcmp(key, "hooks") == 0) {
            /* Parse comma-separated hooks: "keyboard, slice_override[exclusive]" */
            char *token = strtok(val, ",");
            while (token) {
                token = ltrim(token);
                trim(token);

                char hook_name[MAX_NAME];
                int is_exclusive = 0;
                char *bracket = strstr(token, "[exclusive]");
                if (bracket) {
                    *bracket = '\0';
                    strncpy(hook_name, token, MAX_NAME - 1);
                    hook_name[MAX_NAME - 1] = '\0';
                    is_exclusive = 1;
                } else {
                    strncpy(hook_name, token, MAX_NAME - 1);
                    hook_name[MAX_NAME - 1] = '\0';
                }

                if (d->hook_count >= MAX_HOOKS) {
                    fprintf(stderr, "ERROR: too many hooks in '%s' (max %d)\n", path, MAX_HOOKS);
                    fclose(f);
                    return 1;
                }
                int hi = d->hook_count;
                strncpy(d->hooks[hi], hook_name, MAX_NAME - 1);
                d->hooks[hi][MAX_NAME - 1] = '\0';
                if (is_exclusive) {
                    if (strcmp(hook_name, "slice_override") == 0)
                        d->exclusive_mask |= (1 << 0);
                    else if (strcmp(hook_name, "render_overlay") == 0)
                        d->exclusive_mask |= (1 << 1);
                }
                d->hook_count++;
                token = strtok(NULL, ",");
            }
        } else if (strcmp(key, "params") == 0) {
            char *token = strtok(val, ",");
            while (token) {
                token = ltrim(token);
                trim(token);
                if (d->param_count >= MAX_PARAMS) {
                    fprintf(stderr, "ERROR: too many params in '%s' (max %d)\n", path, MAX_PARAMS);
                    fclose(f);
                    return 1;
                }
                strncpy(d->params[d->param_count], token, MAX_NAME - 1);
                d->params[d->param_count][MAX_NAME - 1] = '\0';
                d->param_count++;
                token = strtok(NULL, ",");
            }
        } else if (strcmp(key, "depends") == 0 && strlen(val) > 0) {
            char *token = strtok(val, ",");
            while (token) {
                token = ltrim(token);
                trim(token);
                if (d->depends_count >= MAX_DEPS) {
                    fprintf(stderr, "ERROR: too many depends in '%s' (max %d)\n", path, MAX_DEPS);
                    fclose(f);
                    return 1;
                }
                strncpy(d->depends[d->depends_count], token, MAX_NAME - 1);
                d->depends[d->depends_count][MAX_NAME - 1] = '\0';
                d->depends_count++;
                token = strtok(NULL, ",");
            }
        } else if (strcmp(key, "conflicts") == 0 && strlen(val) > 0) {
            char *token = strtok(val, ",");
            while (token) {
                token = ltrim(token);
                trim(token);
                if (d->conflicts_count >= MAX_CONFLICTS) {
                    fprintf(stderr, "ERROR: too many conflicts in '%s' (max %d)\n", path, MAX_CONFLICTS);
                    fclose(f);
                    return 1;
                }
                strncpy(d->conflicts[d->conflicts_count], token, MAX_NAME - 1);
                d->conflicts[d->conflicts_count][MAX_NAME - 1] = '\0';
                d->conflicts_count++;
                token = strtok(NULL, ",");
            }
        } else if (strcmp(key, "panels") == 0 && strlen(val) > 0) {
            char *token = strtok(val, ",");
            while (token) {
                token = ltrim(token);
                trim(token);
                if (d->panels_count >= MAX_PANELS_DESC) {
                    fprintf(stderr, "WARN: too many panels in '%s' (max %d)\n", path, MAX_PANELS_DESC);
                    break;
                }
                strncpy(d->panels[d->panels_count], token, MAX_NAME - 1);
                d->panels[d->panels_count][MAX_NAME - 1] = '\0';
                d->panels_count++;
                token = strtok(NULL, ",");
            }
        }
    }
    fclose(f);
    return 0;
}

/* Walk src/plugins/ and load .plugin files for every plugin in the profile */
static int discover_plugins(void) {
    for (int i = 0; i < g_entry_count; i++) {
        const char *name = g_entries[i].name;
        char plugin_file[512];
        snprintf(plugin_file, sizeof(plugin_file),
                 "%s/%s/%s.plugin", PLUGINS_DIR, name, name);

        struct stat st;
        if (stat(plugin_file, &st) != 0) {
            fprintf(stderr, "ERROR: plugin '%s' has no descriptor at '%s'\n",
                    name, plugin_file);
            return 1;
        }
        if (g_desc_count >= MAX_PLUGINS) {
            fprintf(stderr, "ERROR: too many plugins (max %d)\n", MAX_PLUGINS);
            return 1;
        }
        if (parse_plugin_file(PLUGINS_DIR, name) != 0) return 1;
        g_desc_count++;
    }
    return 0;
}

static int validate(void) {
    int ok = 1;

    /* 1. Validate that each entry's parameters are declared in its descriptor */
    for (int i = 0; i < g_entry_count; i++) {
        ProfileEntry *pe = &g_entries[i];
        PluginDesc *pd = find_desc(pe->name);
        if (!pd) continue;
        for (int j = 0; j < pe->param_count; j++) {
            int found = 0;
            for (int k = 0; k < pd->param_count; k++) {
                if (strcmp(pe->params[j], pd->params[k]) == 0) {
                    found = 1; break;
                }
            }
            if (!found) {
                fprintf(stderr, "ERROR: plugin '%s' does not declare param '%s'\n",
                        pe->name, pe->params[j]);
                ok = 0;
            }
        }
    }

    /* 2. Dependency satisfaction */
    for (int i = 0; i < g_desc_count; i++) {
        PluginDesc *pd = &g_descs[i];
        if (!in_profile(pd->name)) continue;
        for (int j = 0; j < pd->depends_count; j++) {
            if (!in_profile(pd->depends[j])) {
                fprintf(stderr, "ERROR: plugin '%s' depends on '%s' which is not in profile\n",
                        pd->name, pd->depends[j]);
                ok = 0;
            }
        }
    }

    /* 3. Circular dependency detection (depth-first search) */
    {
        int visited[MAX_PLUGINS];
        int in_stack[MAX_PLUGINS];
        memset(visited, 0, sizeof(visited));
        memset(in_stack, 0, sizeof(in_stack));

        for (int i = 0; i < g_desc_count; i++) {
            if (!in_profile(g_descs[i].name)) continue;
            if (!visited[i]) {
                if (dfs_cycle(i, visited, in_stack)) { ok = 0; break; }
            }
        }
    }

    /* 4 + 7. Direct + transitive conflict checking */
    for (int i = 0; i < g_desc_count; i++) {
        PluginDesc *pd = &g_descs[i];
        if (!in_profile(pd->name)) continue;
        /* Direct conflicts */
        for (int j = 0; j < pd->conflicts_count; j++) {
            if (in_profile(pd->conflicts[j])) {
                fprintf(stderr, "ERROR: plugin '%s' conflicts with '%s' — both in profile\n",
                        pd->name, pd->conflicts[j]);
                ok = 0;
            }
        }
        /* Transitive: check plugins that depend on this one */
        for (int k = 0; k < g_desc_count; k++) {
            if (k == i) continue;
            PluginDesc *other = &g_descs[k];
            if (!in_profile(other->name)) continue;
            for (int d = 0; d < other->depends_count; d++) {
                if (strcmp(other->depends[d], pd->name) == 0) {
                    for (int j = 0; j < pd->conflicts_count; j++) {
                        if (in_profile(pd->conflicts[j])) {
                            fprintf(stderr,
                                "ERROR: plugin '%s' depends on '%s' which conflicts with '%s' in profile\n",
                                other->name, pd->name, pd->conflicts[j]);
                            ok = 0;
                        }
                    }
                }
            }
        }
    }

    /* 5. Exclusive hook overlap */
    {
        int exclusive_used = 0;
        for (int i = 0; i < g_desc_count; i++) {
            PluginDesc *pd = &g_descs[i];
            if (!in_profile(pd->name)) continue;
            if (exclusive_used & pd->exclusive_mask) {
                fprintf(stderr, "ERROR: two plugins register the same exclusive hook\n");
                ok = 0;
            }
            exclusive_used |= pd->exclusive_mask;
        }
    }

    if (!ok) {
        fprintf(stderr, "Validation FAILED — fix errors above and retry.\n");
    }
    return ok ? 0 : 1;
}

static void emit_makefile(void) {
    printf("# Generated by build_tool — DO NOT EDIT\n");
    printf("# Profile: %s\n", g_profile_name);
    printf("\n");
    printf("CFLAGS += -DPLUGIN_REGISTRY_AVAILABLE\n");

    /* Add CFLAGS for each plugin */
    for (int i = 0; i < g_entry_count; i++) {
        ProfileEntry *pe = &g_entries[i];

        /* Lowercase define: -DPLUGIN_<name> */
        printf("CFLAGS += -DPLUGIN_%s\n", pe->name);

        /* Uppercase define: -DPLUGIN_<NAME> */
        printf("CFLAGS += -DPLUGIN_");
        for (int c = 0; pe->name[c]; c++) {
            char ch = (pe->name[c] >= 'a' && pe->name[c] <= 'z')
                      ? (pe->name[c] - 'a' + 'A') : pe->name[c];
            putchar(ch);
        }
        putchar('\n');

        /* Parameter defines: -DPLUGIN_<name>_<param>=<val> */
        for (int j = 0; j < pe->param_count; j++) {
            printf("CFLAGS += -DPLUGIN_%s_%s=%s\n",
                   pe->name, pe->params[j], pe->param_vals[j]);
        }
    }

    printf("\nPLUGIN_SRCS =");
    for (int i = 0; i < g_entry_count; i++)
        printf(" src/plugins/%s/%s.c", g_entries[i].name, g_entries[i].name);
    printf("\nSRCS += $(PLUGIN_SRCS)\n");

    printf("\nPLUGIN_ORDER =");
    for (int i = 0; i < g_entry_count; i++)
        printf(" %s", g_entries[i].name);
    printf("\n");
}

static void emit_plugins_c(void) {
    FILE *f = fopen("build/plugins.c", "w");
    if (!f) { fprintf(stderr, "ERROR: cannot write build/plugins.c\n"); return; }

    fprintf(f, "/* Generated by build_tool — DO NOT EDIT */\n");
    fprintf(f, "#include \"core/app.h\"\n");
    fprintf(f, "#include \"core/plugin.h\"\n");
    fprintf(f, "\n");

    /* Forward-declare plugins */
    for (int i = 0; i < g_entry_count; i++)
        fprintf(f, "extern Plugin %s_plugin;\n", g_entries[i].name);

    /* Emit panel name arrays for each plugin */
    int any_panels = 0;
    for (int i = 0; i < g_entry_count; i++) {
        PluginDesc *d = find_desc(g_entries[i].name);
        if (!d || d->panels_count == 0) continue;
        any_panels = 1;
        fprintf(f, "\nstatic const char *%s_panel_names[] = {\n", g_entries[i].name);
        for (int j = 0; j < d->panels_count; j++)
            fprintf(f, "    \"%s\",\n", d->panels[j]);
        fprintf(f, "    NULL\n");
        fprintf(f, "};\n");
    }

    /* Emit plugin registry */
    fprintf(f, "\nPlugin *plugin_registry[] = {\n");
    for (int i = 0; i < g_entry_count; i++)
        fprintf(f, "    &%s_plugin,\n", g_entries[i].name);
    fprintf(f, "    NULL\n");
    fprintf(f, "};\n");

    /* Emit init function that assigns panel metadata */
    fprintf(f, "\nvoid plugin_init_panels(Plugin **plugins, int count) {\n");
    if (any_panels) {
        fprintf(f, "    for (int i = 0; i < count; i++) {\n");
        fprintf(f, "        Plugin *p = plugins[i];\n");
        for (int i = 0; i < g_entry_count; i++) {
            PluginDesc *d = find_desc(g_entries[i].name);
            if (!d || d->panels_count == 0) continue;
            fprintf(f, "        if (p == &%s_plugin) {\n", g_entries[i].name);
            fprintf(f, "            p->panel_names = %s_panel_names;\n", g_entries[i].name);
            fprintf(f, "            p->num_panels = %d;\n", d->panels_count);
            fprintf(f, "        }\n");
        }
        fprintf(f, "    }\n");
    } else {
        fprintf(f, "    (void)plugins; (void)count;\n");
    }
    fprintf(f, "}\n");

    fprintf(f, "\nint plugin_registry_count = %d;\n", g_entry_count);
    fclose(f);

    fprintf(stderr, "# Generated build/plugins.c with %d plugin(s)\n", g_entry_count);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./build_tool <profile_file>\n");
        return 1;
    }
    if (parse_profile(argv[1]) != 0) return 1;
    if (discover_plugins() != 0)    return 1;
    if (validate() != 0)            return 1;
    emit_makefile();
    emit_plugins_c();
    return 0;
}
