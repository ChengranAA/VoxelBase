/*
 *  vbl_bridge.c — VBL ←→ VoxelBase REPL bridge
 *
 *  Wraps the VBL worker engine (parser → graph → eval).
 *  Uses a global App* to read slot data.  All VBL Values are independent
 *  copies — safe regardless of slot add/delete/reorder.
 *
 *  Bridge ops:
 *    (slot n)         — copy slot n volume → VBL Value
 *    (slot n t)       — copy slot n timepoint t → VBL Value (vol3d)
 *    (show val)       — push VBL value as new VoxelBase slot (via temp .nii)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#include "vbl_bridge.h"
#include "app.h"

/* ── VBL engine internals ────────────────────────────────────── */
#include "worker/parser.h"
#include "worker/graph.h"
#include "worker/env.h"
#include "worker/pool.h"
#include "worker/types.h"
#include "worker/ops.h"

/* ── global pointer for slot lookups ──────────────────────────── */
static App *g_app = NULL;

/* ── crash recovery ──────────────────────────────────────────── */
static sigjmp_buf g_crash_jmp;
static volatile int g_in_eval = 0;

static void crash_handler(int sig) {
    (void)sig;
    if (g_in_eval) siglongjmp(g_crash_jmp, 1);
    else _exit(1);
}

/* ── error capture ────────────────────────────────────────────── */
static char g_err_buf[4096];
static int  g_err_len = 0;

static void err_write(const char *msg) {
    int slen = (int)strlen(msg);
    if (g_err_len + slen < (int)sizeof(g_err_buf) - 1) {
        memcpy(g_err_buf + g_err_len, msg, slen);
        g_err_len += slen;
        g_err_buf[g_err_len] = '\0';
    }
}
static void err_clear(void) { g_err_len = 0; g_err_buf[0] = '\0'; }

/* ── slot ref: zero-copy Value pointing into slot data ────────── */
Value *val_new_slot_ref(int idx) {
    if (!g_app || idx < 0 || idx >= g_app->num_slots) return val_new_nil();
    ImageSlot *s = &g_app->slots[idx];
    if (!s->vol) return val_new_nil();
    Value *v = calloc(1, sizeof(Value));
    v->type = s->nt > 1 ? TYPE_VOLUME4D : TYPE_VOLUME3D;
    v->nx = s->nx; v->ny = s->ny; v->nz = s->nz; v->nt = s->nt > 0 ? s->nt : 1;
    v->dx = s->dx; v->dy = s->dy; v->dz = s->dz; v->tr = s->tr;
    v->data_len = (int64_t)s->nx * s->ny * s->nz * v->nt;
    v->data = s->vol;
    v->owns_data = 1;    /* so ew_bc creates copies, not in-place mutate */
    v->slot_index = idx; /* val_free skips free() when slot_index >= 0 */
    snprintf(v->label, sizeof(v->label), "#<slot-ref %d %dx%dx%dx%d>",
             idx, s->nx, s->ny, s->nz, v->nt);
    return v;
}

/* ── bridge ops ───────────────────────────────────────────────── */

static Value *bridge_op_slot(int argc, Value **args) {
    (void)args;
    if (!g_app || argc < 1) return val_new_nil();

    if (args[0]->type != TYPE_VOLUME3D || args[0]->nx != 1 ||
        args[0]->ny != 1 || args[0]->nz != 1) {
        err_write("ERROR: (slot n) expects integer n\n");
        return val_new_nil();
    }
    int n = (int)(args[0]->data[0] + 0.5f);
    if (n < 0 || n >= g_app->num_slots) {
        err_write("ERROR: slot index out of range\n");
        return val_new_nil();
    }

    ImageSlot *s = &g_app->slots[n];
    if (!s->vol || s->nx <= 0) {
        err_write("ERROR: slot has no volume data\n");
        return val_new_nil();
    }
    if (s->nt > 1 && s->loaded_t_count < s->nt && argc < 2) {
        err_write("ERROR: full 4D slot is still loading; use (slot n 0) for t=0 or wait\n");
        return val_new_nil();
    }
    /* for huge volumes, return zero-copy ref instead of copying */
    int64_t total = (int64_t)s->nx * s->ny * s->nz * (s->nt > 1 ? s->nt : 1);
    int use_ref = (total > 200000000);

    int t = -1;
    if (argc >= 2 && args[1]->type == TYPE_VOLUME3D &&
        args[1]->nx == 1 && args[1]->ny == 1 && args[1]->nz == 1) {
        t = (int)(args[1]->data[0] + 0.5f);
    }

    if (s->nt > 1 && t < 0) {
        if (use_ref) return val_new_slot_ref(n);
        return val_new_volume4d_from_buf(s->nx, s->ny, s->nz, s->nt,
                                          s->dx, s->dy, s->dz, s->tr, s->vol);
    } else if (s->nt > 1 && t >= 0) {
        int available_t = s->loaded_t_count > 0 ? s->loaded_t_count : s->nt;
        if (t >= available_t) {
            err_write("ERROR: requested timepoint is not loaded yet\n");
            return val_new_nil();
        }
        int64_t offset = (int64_t)t * s->nx * s->ny * s->nz;
        if (use_ref) {
            Value *r = val_new_slot_ref(n);
            r->type = TYPE_VOLUME3D; r->nt = 1;
            r->data = s->vol + offset;
            r->data_len = (int64_t)s->nx * s->ny * s->nz;
            return r;
        }
        return val_new_volume3d_from_buf(s->nx, s->ny, s->nz,
                                          s->dx, s->dy, s->dz,
                                          s->vol + offset);
    } else {
        if (use_ref) return val_new_slot_ref(n);
        return val_new_volume3d_from_buf(s->nx, s->ny, s->nz,
                                          s->dx, s->dy, s->dz, s->vol);
    }
}

static Value *bridge_op_show(int argc, Value **args) {
    if (argc < 1 || !g_app) return val_new_nil();
    Value *v = args[0];
    if (!v || !v->data) {
        err_write("ERROR: (show) empty value\n");
        return val_new_nil();
    }
    if (g_app->num_slots >= MAX_SLOTS) {
        err_write("ERROR: (show) max slots reached\n");
        return val_new_nil();
    }
    /* free previous pending data if any */
    free(g_app->repl_pending_vol);
    g_app->repl_pending_vol = NULL;
    int nt = v->nt > 0 ? v->nt : 1;
    int64_t nvox = (int64_t)(v->nx > 0 ? v->nx : 1) *
                   (v->ny > 0 ? v->ny : 1) *
                   (v->nz > 0 ? v->nz : 1) * nt;
    if (nvox < 1) nvox = 1;
    g_app->repl_pending_vol = malloc((size_t)nvox * sizeof(float));
    if (!g_app->repl_pending_vol) {
        err_write("ERROR: (show) out of memory\n");
        return val_new_nil();
    }
    memcpy(g_app->repl_pending_vol, v->data, (size_t)nvox * sizeof(float));
    g_app->repl_pending_nx  = v->nx > 0 ? v->nx : 1;
    g_app->repl_pending_ny  = v->ny > 0 ? v->ny : 1;
    g_app->repl_pending_nz  = v->nz > 0 ? v->nz : 1;
    g_app->repl_pending_nt  = nt;
    g_app->repl_pending_dx  = v->dx;
    g_app->repl_pending_dy  = v->dy;
    g_app->repl_pending_dz  = v->dz;
    g_app->repl_pending_tr  = v->tr;
    g_app->repl_pending_has_data = 1;
    if (g_app->repl_out_count < 200)
        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                 "  [show] %dx%dx%dx%d", v->nx, v->ny, v->nz, nt);
    return val_new_nil();
}

/* ── bridge plot ops ─────────────────────────────────────────── */
static Color s_palette[] = {
    {100,220,180,255}, {255,180,80,255}, {220,100,160,255},
    {80,180,255,255},  {180,255,100,255}, {255,130,100,255},
    {160,130,255,255}, {255,220,60,255},
};

static void plot_add_series(App *app, Value *ys, Value *xs, const char *name) {
    int si = app->plot_series_count;
    if (si >= MAX_PLOT_SERIES) {
        err_write("ERROR: max 8 plot series reached\n");
        return;
    }
    /* reject multi-dimensional data — user must flatten first */
    if (ys->type == TYPE_VOLUME3D || ys->type == TYPE_VOLUME4D) {
        err_write("ERROR: plot-line expects 1D data. "
                  "Use (tmean ...), (voxel ...), or (slice ...) first.\n");
        return;
    }
    int n = ys->nt > 1 ? ys->nt : (ys->nx * ys->ny * ys->nz);
    if (n < 1) { err_write("ERROR: empty data\n"); return; }
    if (n > MAX_PLOT_POINTS) {
        n = MAX_PLOT_POINTS;
        err_write("WARN: data truncated to 512 points\n");
    }
    int xsn = xs ? (xs->nt > 1 ? xs->nt : (xs->nx * xs->ny * xs->nz)) : n;
    app->plot_series[si].count = n;
    app->plot_series[si].color = s_palette[si % 8];
    snprintf(app->plot_series[si].name, sizeof(app->plot_series[si].name),
             "%s", name ? name : "series");
    int64_t ys_stride = ys->nt > 1 ? (int64_t)ys->nx * ys->ny * ys->nz : 1;
    if (ys_stride < 1) ys_stride = 1;
    int64_t xs_stride = xs ? (xs->nt > 1 ? (int64_t)xs->nx * xs->ny * xs->nz : 1) : 1;
    if (xs_stride < 1) xs_stride = 1;
    for (int i = 0; i < n; i++) {
        app->plot_series[si].ys[i] = ys->data[i * ys_stride];
        app->plot_series[si].xs[i] = xs ? xs->data[(i % xsn) * xs_stride] : (float)i;
    }
    app->plot_series_count++;
}

static Value *bridge_op_plot_line(int argc, Value **args) {
    if (!g_app || argc < 1) return val_new_nil();
    g_app->plot_series_count = 0;
    g_app->plot_hist_count = 0;
    /* optional name as string arg */
    const char *name = NULL;
    int arg_off = 0;
    if (args[0]->type == TYPE_VOLUME3D && args[0]->nx == 1 &&
        args[0]->ny == 1 && args[0]->nz == 1 && argc >= 3 &&
        args[0]->data[0] == 0) {
        /* first arg is scalar 0 = no name */
    }
    plot_add_series(g_app, args[arg_off], (argc >= 2+arg_off) ? args[1+arg_off] : NULL, name);
    g_app->repl_mode = 1;
    return val_new_nil();
}

static Value *bridge_op_add_line(int argc, Value **args) {
    if (!g_app || argc < 1) return val_new_nil();
    Value *ys = args[0];
    if (!ys || !ys->data || ys->type == TYPE_NIL) {
        err_write("ERROR: add-line needs valid data\n");
        return val_new_nil();
    }
    Value *xs = (argc >= 2) ? args[1] : NULL;
    char name[32];
    snprintf(name, sizeof(name), "S%d", g_app->plot_series_count + 1);
    plot_add_series(g_app, ys, xs, name);
    g_app->repl_mode = 1;
    return val_new_nil();
}

static Value *bridge_op_plot_hist(int argc, Value **args) {
    if (!g_app || argc < 1) return val_new_nil();
    Value *v = args[0];
    int nbins = (argc >= 2) ? (int)(args[1]->data[0] + 0.5f) : 16;
    if (nbins < 2) nbins = 2;
    if (nbins > 64) nbins = 64;
    int n = v->nt > 1 ? v->nt : (v->nx * v->ny * v->nz);
    if (n < 1) { err_write("ERROR: empty data\n"); return val_new_nil(); }
    float lo = v->data[0], hi = v->data[0];
    for (int i = 0; i < n; i++) {
        if (v->data[i] < lo) lo = v->data[i];
        if (v->data[i] > hi) hi = v->data[i];
    }
    float range = hi - lo; if (range < 0.0001f) range = 1.0f;
    g_app->plot_hist_count = nbins;
    g_app->plot_hist_min = lo;
    g_app->plot_hist_max = hi;
    g_app->plot_series_count = 0;
    memset(g_app->plot_hist, 0, sizeof(g_app->plot_hist));
    for (int i = 0; i < n; i++) {
        int b = (int)((v->data[i] - lo) / range * (float)nbins);
        if (b < 0) b = 0;
        if (b >= nbins) b = nbins - 1;
        g_app->plot_hist[b]++;
    }
    snprintf(g_app->plot_xlabel, sizeof(g_app->plot_xlabel), "%s", "value");
    snprintf(g_app->plot_ylabel, sizeof(g_app->plot_ylabel), "%s", "count");
    g_app->repl_mode = 1;
    return val_new_nil();
}

static Value *bridge_op_clear(int argc, Value **args) {
    (void)argc; (void)args;
    if (!g_app) return val_new_nil();
    g_app->repl_out_count = 0;
    g_app->repl_scroll = 0;
    g_app->repl_mode = 0;
    g_app->plot_series_count = 0;
    g_app->plot_hist_count = 0;
    g_app->plot_xlabel[0] = '\0';
    g_app->plot_ylabel[0] = '\0';
    return val_new_nil();
}

/* ── bridge drift: reads slot data directly, no copy ──────────── */
static Value *bridge_op_drift(int argc, Value **args) {
    if (!g_app || argc < 1) return val_new_nil();
    if (args[0]->type != TYPE_VOLUME3D || args[0]->nx != 1 ||
        args[0]->ny != 1 || args[0]->nz != 1) {
        err_write("ERROR: (drift n [stride]) expects integer slot index\n");
        return val_new_nil();
    }
    int n = (int)(args[0]->data[0] + 0.5f);
    if (n < 0 || n >= g_app->num_slots) {
        err_write("ERROR: drift slot index out of range\n");
        return val_new_nil();
    }
    ImageSlot *s = &g_app->slots[n];
    if (!s->vol || s->nt < 2) {
        err_write("ERROR: drift needs 4D slot with >= 2 timepoints\n");
        return val_new_nil();
    }
    if (s->loaded_t_count < s->nt) {
        err_write("ERROR: drift needs the full 4D slot; background load is still running\n");
        return val_new_nil();
    }
    int factor = (argc >= 2 && args[1]->type == TYPE_VOLUME3D &&
                  args[1]->nx == 1 && args[1]->ny == 1 && args[1]->nz == 1)
                 ? (int)(args[1]->data[0] + 0.5f) : 1;
    if (factor < 1) factor = 1;

    int nx = s->nx, ny = s->ny, nz = s->nz, nt = s->nt;
    int64_t n3 = (int64_t)nx * ny * nz;
    int64_t n_eff = (n3 + factor - 1) / factor;
    Value *r = val_new_timeseries(nt, s->tr);
    /* preload frame 0 for fast reference */
    float *f0 = malloc((size_t)n3 * sizeof(float));
    for (int64_t i = 0; i < n3; i++) f0[i] = s->vol[i];
    for (int t = 0; t < nt; t++) {
        double sum = 0.0;
        int64_t count = 0;
        for (int64_t i = 0; i < n3; i += factor) {
            if (fabsf(f0[i]) < 1e-6f) continue;  /* background */
            sum += (double)fabsf(s->vol[t * n3 + i] - f0[i]);
            count++;
        }
        r->data[t] = count > 0 ? (float)(sum / (double)count) : 0.0f;
    }
    free(f0);
    snprintf(r->label, sizeof(r->label), "(drift slot %d)", n);
    return r;
}

/* ── bridge-aware graph builder ───────────────────────────────── */
/*
 *  Intercepts (slot n) and (show val) to create bridge graph nodes.
 *  Registered as a hook in graph_build via graph_set_bridge_hook().
 *  Returns GraphNode* or NULL to fall through.
 */
static GraphNode *bridge_graph_hook(const char *op_name, AstNode *ast) {
    if (strcmp(op_name, "slot") == 0) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef slot_op_def = {"slot", bridge_op_slot, 1, 2};
        g->op = &slot_op_def;

        if (ast->list.count >= 2) {
            g->args = malloc(sizeof(GraphNode *));
            g->args[0] = graph_build(ast->list.items[1]);
            g->arg_count = 1;
        }
        if (ast->list.count >= 3) {
            g->args = realloc(g->args, 2 * sizeof(GraphNode *));
            g->args[1] = graph_build(ast->list.items[2]);
            g->arg_count = 2;
        }
        return g;
    }

    if (strcmp(op_name, "show") == 0 && ast->list.count >= 2) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef show_op_def = {"show", bridge_op_show, 1, 1};
        g->op = &show_op_def;
        g->args = malloc(sizeof(GraphNode *));
        g->args[0] = graph_build(ast->list.items[1]);
        g->arg_count = 1;
        return g;
    }

    if (strcmp(op_name, "plot-line") == 0 && ast->list.count >= 2) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef op_def = {"plot-line", bridge_op_plot_line, 1, 2};
        g->op = &op_def;
        g->args = malloc(sizeof(GraphNode *));
        g->args[0] = graph_build(ast->list.items[1]);
        g->arg_count = 1;
        if (ast->list.count >= 3) {
            g->args = realloc(g->args, 2 * sizeof(GraphNode *));
            g->args[1] = graph_build(ast->list.items[2]);
            g->arg_count = 2;
        }
        return g;
    }

    if (strcmp(op_name, "add-line") == 0 && ast->list.count >= 2) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef op_def = {"add-line", bridge_op_add_line, 1, 2};
        g->op = &op_def;
        g->args = malloc(sizeof(GraphNode *));
        g->args[0] = graph_build(ast->list.items[1]);
        g->arg_count = 1;
        if (ast->list.count >= 3) {
            g->args = realloc(g->args, 2 * sizeof(GraphNode *));
            g->args[1] = graph_build(ast->list.items[2]);
            g->arg_count = 2;
        }
        return g;
    }

    if (strcmp(op_name, "plot-hist") == 0 && ast->list.count >= 2) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef op_def = {"plot-hist", bridge_op_plot_hist, 1, 2};
        g->op = &op_def;
        g->args = malloc(sizeof(GraphNode *));
        g->args[0] = graph_build(ast->list.items[1]);
        g->arg_count = 1;
        if (ast->list.count >= 3) {
            g->args = realloc(g->args, 2 * sizeof(GraphNode *));
            g->args[1] = graph_build(ast->list.items[2]);
            g->arg_count = 2;
        }
        return g;
    }

    if (strcmp(op_name, "clear") == 0) {
        GraphNode *g = calloc(1, sizeof(GraphNode));
        g->type = GN_CALL;
        static OpDef op_def = {"clear", bridge_op_clear, 0, 0};
        g->op = &op_def;
        g->arg_count = 0;
        return g;
    }

    if (strcmp(op_name, "drift") == 0 && ast->list.count >= 2) {
        /* if (drift (slot n) ...), extract index → zero-copy bridge op */
        AstNode *a1 = ast->list.items[1];
        if (a1->type == AST_LIST && a1->list.count >= 2 &&
            a1->list.items[0]->type == AST_SYMBOL &&
            strcmp(a1->list.items[0]->symbol, "slot") == 0) {
            GraphNode *g = calloc(1, sizeof(GraphNode));
            g->type = GN_CALL;
            static OpDef op_def = {"drift", bridge_op_drift, 1, 2};
            g->op = &op_def;
            g->args = malloc(sizeof(GraphNode *));
            g->args[0] = graph_build(a1->list.items[1]); /* slot index */
            g->arg_count = 1;
            if (ast->list.count >= 3) {
                g->args = realloc(g->args, 2 * sizeof(GraphNode *));
                g->args[1] = graph_build(ast->list.items[2]); /* stride */
                g->arg_count = 2;
            }
            return g;
        }
        /* not (slot ...) — fall through to op_drift in ops.c */
        return NULL;
    }

    return NULL;
}

/* ── forward declarations ─────────────────────────────────────── */
static GraphNode *bridge_build_with_hook(AstNode *ast);
static GraphNode *make_show_node(GraphNode *sub);
static char *capture_stdout_cb(Value *v);

/* ── public API ───────────────────────────────────────────────── */

void vbl_bridge_init(void *app_ptr) {
    g_app = (App *)app_ptr;
    env_init();
    g_pool = pool_create(0);
    graph_set_bridge_hook(bridge_graph_hook);
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
}

void vbl_bridge_shutdown(void) {
    if (g_pool) pool_destroy(g_pool);
    g_pool = NULL;
    env_shutdown();
    g_app = NULL;
}

/*
 *  Evaluate a VBL S-expression.  Writes output to app->repl_output.
 *  Returns 0 on success, <0 on error.
 */
int vbl_bridge_eval(void *app_ptr, const char *input) {
    (void)app_ptr;
    err_clear();
    int retval = 0;

    /* ── crash recovery ───────────────────────────────────── */
    g_in_eval = 1;
    if (sigsetjmp(g_crash_jmp, 1) != 0) {
        /* crashed — restore state and report */
        g_in_eval = 0;
        signal(SIGSEGV, crash_handler);
        signal(SIGBUS, crash_handler);
        signal(SIGABRT, crash_handler);
        signal(SIGFPE, crash_handler);
        /* reinit VBL environment */
        if (g_pool) pool_destroy(g_pool);
        env_shutdown();
        env_init();
        g_pool = pool_create(0);
        graph_set_bridge_hook(bridge_graph_hook);
        if (g_app->repl_out_count < 200)
            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                     "  !! CRASH — REPL reset. VBL env cleared.");
        return -1;
    }

    /* ── capture stderr (worker type errors etc.) ──────────── */
    int err_pipe[2] = {-1, -1};
    int saved_stderr = -1;
    if (pipe(err_pipe) == 0) {
        saved_stderr = dup(STDERR_FILENO);
        if (saved_stderr >= 0) {
            dup2(err_pipe[1], STDERR_FILENO);
            close(err_pipe[1]);
            err_pipe[1] = -1;
        } else {
            close(err_pipe[0]); close(err_pipe[1]);
            err_pipe[0] = err_pipe[1] = -1;
        }
    }

    /* skip whitespace */
    while (*input == ' ' || *input == '\t') input++;
    if (*input == '\0' || *input == '\n') goto done;

    /* comment line */
    if (*input == ';') {
        if (g_app->repl_out_count < 200) {
            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                     "  %s", input);
        }
        goto done;
    }

    /* check paren balance */
    {
        int depth = 0;
        for (const char *p = input; *p; p++) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
        }
        if (depth != 0) {
            if (g_app->repl_out_count < 200)
                snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                         "  (unbalanced parentheses)");
            retval = -1;
            goto done;
        }
    }

    /* parse */
    char *input_copy = strdup(input);
    AstNode *ast = parse_expr(input_copy);
    free(input_copy);

    if (!ast) {
        if (g_app->repl_out_count < 200)
            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                     "  parse error");
        retval = -1;
        goto done;
    }

    /* ── dispatch: special forms ──────────────────────────────── */
    if (ast->type == AST_LIST && ast->list.count > 0 &&
        ast->list.items[0]->type == AST_SYMBOL) {
        const char *cmd = ast->list.items[0]->symbol;

        /* exit */
        if (strcmp(cmd, "exit") == 0) { ast_free(ast); goto done; }

        /* (repl-reset) — reinitialize VBL environment */
        if (strcmp(cmd, "repl-reset") == 0) {
            if (g_pool) pool_destroy(g_pool);
            env_shutdown();
            env_init();
            g_pool = pool_create(0);
            graph_set_bridge_hook(bridge_graph_hook);
            if (g_app->repl_out_count < 200)
                snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                         "  REPL reset. VBL env cleared.");
            ast_free(ast);
            goto done;
        }

        /* (help) / (help op) — list ops, or filter by type/name */
        if (strcmp(cmd, "help") == 0) {
            typedef struct { const char *name, *sig, *desc, *tags; } OpInfo;
            static const OpInfo ops[] = {
                /* name         signature                          description                               tags */
                {"+",        "(+ a b)",             "element-wise add, same-shape vols",        "vol3d vol4d"},
                {"-",        "(- a b)",             "element-wise subtract",                    "vol3d vol4d"},
                {"*",        "(* a b)",             "element-wise multiply",                    "vol3d vol4d"},
                {"/",        "(/ a b)",             "element-wise divide",                      "vol3d vol4d"},
                {"bc+",      "(bc+ vol scalar)",    "broadcast add scalar to every voxel",      "vol3d vol4d scalar"},
                {"bc-",      "(bc- vol scalar)",    "broadcast subtract",                       "vol3d vol4d scalar"},
                {"bc*",      "(bc* vol scalar)",    "broadcast multiply",                       "vol3d vol4d scalar"},
                {"bc/",      "(bc/ vol scalar)",    "broadcast divide",                          "vol3d vol4d scalar"},
                {"mean",     "(mean vol)",          "global scalar (3D) or temporal mean (4D)",  "vol3d vol4d →scalar →vol3d"},
                {"tmean",    "(tmean vol4d t0 t1)", "temporal mean over [t0, t1]",              "vol4d →vol3d"},
                {"stdev",    "(stdev vol)",         "global stdev (3D) or temporal stdev (4D)",  "vol3d vol4d →scalar →vol3d"},
                {"min",      "(min vol)",           "global min (3D) or temporal min (4D)",      "vol3d vol4d →scalar →vol3d"},
                {"max",      "(max vol)",           "global max (3D) or temporal max (4D)",      "vol3d vol4d →scalar →vol3d"},
                {"sum",      "(sum vol)",           "global sum (3D) or temporal sum (4D)",      "vol3d vol4d →scalar →vol3d"},
                {"tstd",     "(tstd vol4d)",        "temporal stdev → vol3d",                    "vol4d →vol3d"},
                {"smooth",   "(smooth vol fwhm)",   "Gaussian smooth, fwhm in voxels/TRs",   "vol3d vol4d timeseries"},
                {"crop",     "(crop v x0 x1 y0 y1 z0 z1)","crop to bounding box",              "vol3d vol4d"},
                {"pad",      "(pad v nx ny nz px py pz)","pad to size with margin",             "vol3d vol4d"},
                {"threshold","(threshold vol val)", "binary mask: 1 where >val else 0",         "vol3d →mask"},
                {"mask",     "(mask vol thresh)",   "float mask, voxels > thresh kept",         "vol3d →mask"},
                {"mask-int", "(mask-int vol th dflt)","integer label mask",                     "vol3d →mask"},
                {"correlate","(correlate vol4d seed)","Pearson r with seed timeseries",         "vol4d timeseries →corrmap"},
                {"seed",     "(seed vol4d x y z)",  "extract timeseries at voxel",              "vol4d →timeseries"},
                {"bandpass", "(bandpass vol4d lo hi)","temporal bandpass filter",               "vol4d timeseries"},
                {"detrend",  "(detrend vol4d)",     "linear detrend per voxel/ts",              "vol4d timeseries"},
                {"drift",      "(drift vol4d [stride])","cumulative intensity drift displacement \u2192 timeseries","vol4d"},
                {"noise",    "(noise nx ny nz sig)","Gaussian noise volume",                    "→vol3d scalar"},
                {"vol3d",    "(vol3d nx ny nz [val])","constant 3D volume",                     "→vol3d scalar"},
                {"vol4d",    "(vol4d nx ny nz nt [val])","constant 4D volume",                  "→vol4d scalar"},
                {"slice",    "(slice vol axis coord)","2D slice view (axis 0/1/2)",             "vol3d vol4d →vol3d"},
                {"tslice",   "(tslice vol4d t)",    "3D volume at timepoint t",                 "vol4d →vol3d"},
                {"ts-range", "(ts-range ts t0 t1)",  "extract timeseries sub-range",              "timeseries"},
                {"copy",     "(copy vol)",          "independent deep copy",                     "vol3d vol4d mask corrmap"},
                {"voxel",    "(voxel vol x y z [t])","scalar at voxel coords",                  "vol3d vol4d →scalar"},
                {"eq",       "(eq a b)",            "element-wise equality mask",                "vol3d vol4d →mask"},
                {"gt",       "(gt a b)",            "element-wise greater-than mask",            "vol3d vol4d →mask"},
                {"lt",       "(lt a b)",            "element-wise less-than mask",               "vol3d vol4d →mask"},
                {"and",      "(and a b)",           "element-wise logical AND",                  "mask"},
                {"or",       "(or a b)",            "element-wise logical OR",                   "mask"},
                {"affine",   "(affine)",            "identity 4×4 affine matrix",                "→affine"},
                {"translate","(translate dx dy dz)", "translation matrix",                        "→affine scalar"},
                {"rotate",   "(rotate deg axis)",   "rotation matrix (axis 0/1/2)",             "→affine scalar"},
                {"scale",    "(scale sx sy sz)",    "scaling matrix",                            "→affine scalar"},
                {"apply",    "(apply affine vol)",  "transform volume by matrix",                "affine vol3d vol4d"},
                {"slot",     "(slot n [t])",        "copy VoxelBase slot n as VBL Value",      "→vol3d →vol4d scalar"},
                {"show",     "(show val)",          "push VBL Value as new VoxelBase slot",     "vol3d vol4d"},
                {"plot-line","(plot-line ys [xs])", "line+scatter plot in canvas",               "scalar timeseries"},
                {"add-line", "(add-line ys [xs])", "add series to existing plot",               "scalar timeseries"},
                {"plot-hist","(plot-hist data [n])","histogram with n bins",                     "scalar vol3d"},
                {"def",      "(def name expr)",     "bind Value to name",                         "any"},
                {"print",    "(print expr)",        "inspect Value",                              "any"},
                {"save",     "(save val \"path\")",  "save Value as .nii",                       "vol3d vol4d mask"},
                {"load",     "(load \"path.nii\")",  "load .nii into VBL Value",                 "→vol3d →vol4d"},
                {"load-script","(load-script \"path.vbl\")","run a .vbl script file",              "any"},
                {"clear",    "(clear)",             "clear output + plots",                       "any"},
                {"repl-reset","(repl-reset)",       "reinitialize VBL environment",                "any"},
                {"if",       "(if cond then [else])","conditional: eval then if truthy, else otherwise","any"},
                {"vars",     "(vars)",              "list all defined variables",                   "any"},
            };
            int n_ops = (int)(sizeof(ops) / sizeof(ops[0]));

            /* (help op) — exact name, then type filter */
            if (ast->list.count >= 2 && ast->list.items[1] &&
                ast->list.items[1]->type == AST_SYMBOL) {
                const char *target = ast->list.items[1]->symbol;

                /* always try type filter first (covers both pure types and op-name-as-type) */
                int shown = 0;
                for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++) {
                    if (strstr(ops[i].tags, target)) {
                        if (!shown && g_app->repl_out_count < 200)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  ── ops for: %s ──", target);
                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                 "  %-12s %s", ops[i].sig, ops[i].desc);
                        shown++;
                    }
                }
                /* also try exact op name if not already covered by type filter */
                if (!shown) {
                    for (int i = 0; i < n_ops; i++) {
                        if (strcmp(ops[i].name, target) == 0) {
                            if (g_app->repl_out_count < 200)
                                snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                         "  %s  %s", ops[i].sig, ops[i].desc);
                            shown = 1;
                            break;
                        }
                    }
                }
                if (!shown && g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ? unknown: %s  — try (help) for full list", target);
            } else {
                /* (help) — list all by category */
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ── Special Forms ──");
                const char *sf[] = {"def","print","save","load","load-script","slot","show","if","vars","clear","repl-reset",NULL};
                for (int j = 0; sf[j]; j++)
                    for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++)
                        if (strcmp(ops[i].name, sf[j]) == 0)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  %-12s %s", ops[i].sig, ops[i].desc);
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ── Arithmetic ──");
                const char *ar[] = {"+","-","*","/","bc+","bc-","bc*","bc/",NULL};
                for (int j = 0; ar[j]; j++)
                    for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++)
                        if (strcmp(ops[i].name, ar[j]) == 0)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  %-12s %s", ops[i].sig, ops[i].desc);
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ── Stats ──");
                const char *st[] = {"mean","tmean","stdev","min","max","sum","tstd",NULL};
                for (int j = 0; st[j]; j++)
                    for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++)
                        if (strcmp(ops[i].name, st[j]) == 0)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  %-12s %s", ops[i].sig, ops[i].desc);
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ── Spatial + Signal ──");
                const char *sp[] = {"smooth","crop","pad","threshold","mask","mask-int",
                                    "correlate","seed","bandpass","detrend","drift","noise",NULL};
                for (int j = 0; sp[j]; j++)
                    for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++)
                        if (strcmp(ops[i].name, sp[j]) == 0)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  %-12s %s", ops[i].sig, ops[i].desc);
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ── Views + Compare + Affine + Plot ──");
                const char *vc[] = {"vol3d","vol4d","slice","tslice","ts-range","copy","voxel",
                                    "eq","gt","lt","and","or",
                                    "affine","translate","rotate","scale","apply",
                                    "plot-line","add-line","plot-hist",NULL};
                for (int j = 0; vc[j]; j++)
                    for (int i = 0; i < n_ops && g_app->repl_out_count < 200; i++)
                        if (strcmp(ops[i].name, vc[j]) == 0)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  %-12s %s", ops[i].sig, ops[i].desc);
            }
            ast_free(ast);
            goto done;
        }

        /* (def name expr) */
        if (strcmp(cmd, "def") == 0 && ast->list.count >= 3 &&
            ast->list.items[1]->type == AST_SYMBOL) {
            const char *name = ast->list.items[1]->symbol;
            /* reject reserved words */
            static const char *reserved[] = {
                "def","print","show","save","vars","exit","help",
                "slot","load","load-script","repl-reset","clear",
                "plot-line","add-line","plot-hist","if", NULL
            };
            int blocked = 0;
            for (int ri = 0; reserved[ri]; ri++)
                if (strcmp(name, reserved[ri]) == 0) { blocked = 1; break; }
            if (!blocked && op_find(name)) blocked = 1;
            if (blocked) {
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ERROR: '%s' is reserved", name);
                ast_free(ast);
                goto check_err;
            }
            GraphNode *gn = bridge_build_with_hook(ast->list.items[2]);
            if (gn && graph_eval(gn) == 0) {
                env_bind(name, gn->result);
                gn->result = NULL;
                gn->literal = NULL;
            }
            graph_free(gn);
            ast_free(ast);
            goto check_err;
        }

        /* (if cond then [else]) */
        if (strcmp(cmd, "if") == 0 && ast->list.count >= 3) {
            GraphNode *cg = bridge_build_with_hook(ast->list.items[1]);
            int truthy = 0;
            if (cg && graph_eval(cg) == 0 && cg->result) {
                Value *cv = cg->result;
                truthy = cv->is_int ? (cv->idata && cv->idata[0] != 0)
                                    : (cv->data  && cv->data[0]  != 0.0f);
            }
            graph_free(cg);
            AstNode *branch = truthy ? ast->list.items[2]
                         : (ast->list.count >= 4 ? ast->list.items[3] : NULL);
            if (branch) {
                GraphNode *bg = bridge_build_with_hook(branch);
                if (bg && graph_eval(bg) == 0 && bg->result) {
                    char *out = capture_stdout_cb(bg->result);
                    if (out) {
                        if (g_app->repl_out_count < 200)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128, "%s", out);
                        free(out);
                    }
                }
                graph_free(bg);
            }
            ast_free(ast);
            goto check_err;
        }

        /* (print expr) — capture val_print output */
        if (strcmp(cmd, "print") == 0 && ast->list.count >= 2) {
            /* strings: print directly */
            if (ast->list.items[1]->type == AST_STRING) {
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  \"%s\"", ast->list.items[1]->string);
                ast_free(ast);
                goto check_err;
            }
            GraphNode *gn = bridge_build_with_hook(ast->list.items[1]);
            if (gn && graph_eval(gn) == 0 && gn->result) {
                char *out = capture_stdout_cb(gn->result);
                if (out && g_app->repl_out_count < 200) {
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  %s", out);
                }
                free(out);
            }
            graph_free(gn);
            ast_free(ast);
            goto check_err;
        }

        /* (vars) — list all defined variables */
        if (strcmp(cmd, "vars") == 0) {
            int n = env_count();
            if (n == 0) {
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  (no variables defined)");
            } else {
                for (int vi = 0; vi < n; vi++) {
                    const char *vname; Value *vv;
                    env_get(vi, &vname, &vv);
                    const char *tn = "?";
                    if (vv) switch (vv->type) {
                        case TYPE_VOLUME3D: tn="vol3d"; break;
                        case TYPE_VOLUME4D: tn="vol4d"; break;
                        case TYPE_TIMESERIES: tn="ts"; break;
                        case TYPE_CORRMAP: tn="corrmap"; break;
                        case TYPE_MASK: tn="mask"; break;
                        case TYPE_NIL: tn="nil"; break;
                        case TYPE_AFFINE: tn="affine"; break;
                    }
                    if (vv && vv->type==TYPE_VOLUME3D && vv->nx==1 && vv->ny==1 && vv->nz==1)
                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                 "  %-20s %s = %.6g", vname, tn, vv->data[0]);
                    else if (vv)
                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                 "  %-20s %s %dx%dx%d", vname, tn, vv->nx, vv->ny, vv->nz);
                    else
                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                 "  %-20s (null)", vname);
                }
            }
            ast_free(ast);
            goto check_err;
        }

        /* (show expr) — push to new slot */
        if (strcmp(cmd, "show") == 0 && ast->list.count >= 2) {
            GraphNode *gn = bridge_build_with_hook(ast->list.items[1]);
            GraphNode *show_gn = make_show_node(gn);
            if (graph_eval(show_gn) == 0) {
                val_free(show_gn->result);
                show_gn->result = NULL;
            }
            graph_free(show_gn);
            ast_free(ast);
            goto check_err;
        }

        /* (load-script "path.vbl") — run a .vbl file */
        if (strcmp(cmd, "load-script") == 0 && ast->list.count >= 2 &&
            ast->list.items[1]->type == AST_STRING) {
            const char *spath = ast->list.items[1]->string;
            FILE *sf = fopen(spath, "r");
            if (!sf) {
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  cannot open: %s", spath);
            } else {
                char sline[4096];
                int ln = 0;
                while (fgets(sline, sizeof(sline), sf)) {
                    ln++;
                    char *ss = sline;
                    while (*ss == ' ' || *ss == '\t') ss++;
                    if (*ss == '\0' || *ss == '\n' || *ss == ';') continue;
                    /* strip trailing newline */
                    int sl = (int)strlen(ss);
                    while (sl > 0 && (ss[sl-1] == '\n' || ss[sl-1] == '\r')) ss[--sl] = '\0';
                    /* for def, use shortcut; otherwise full eval */
                    char *scopy = strdup(ss);
                    AstNode *sa = parse_expr(scopy);
                    free(scopy);
                    if (!sa) {
                        if (g_app->repl_out_count < 200)
                            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                     "  [line %d] parse error", ln);
                        continue;
                    }
                    if (sa->type == AST_LIST && sa->list.count > 0 &&
                        sa->list.items[0]->type == AST_SYMBOL) {
                        AstNode *sh = sa->list.items[0];
                        if (strcmp(sh->symbol, "def") == 0 &&
                            sa->list.count >= 3 && sa->list.items[1]->type == AST_SYMBOL) {
                            const char *sname = sa->list.items[1]->symbol;
                            static const char *resv[] = {
                                "def","print","show","save","vars","exit","help",
                                "slot","load","load-script","repl-reset","clear",
                                "plot-line","add-line","plot-hist","if", NULL
                            };
                            int blk = 0;
                            for (int ri = 0; resv[ri]; ri++)
                                if (strcmp(sname, resv[ri]) == 0) { blk = 1; break; }
                            if (!blk && op_find(sname)) blk = 1;
                            if (blk) {
                                if (g_app->repl_out_count < 200)
                                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                             "  [line %d] ERROR: '%s' is reserved", ln, sname);
                                ast_free(sa); free(scopy);
                                continue;
                            }
                            GraphNode *gn = bridge_build_with_hook(sa->list.items[2]);
                            if (gn && graph_eval(gn) == 0) {
                                env_bind(sname, gn->result);
                                gn->result = NULL; gn->literal = NULL;
                            }
                            graph_free(gn);
                        } else if (strcmp(sh->symbol, "vars") == 0) {
                            int n = env_count();
                            for (int vi = 0; vi < n && g_app->repl_out_count < 200; vi++) {
                                const char *vn; Value *vv;
                                env_get(vi, &vn, &vv);
                                const char *tn = "?";
                                if (vv) switch (vv->type) {
                                    case TYPE_VOLUME3D: tn="vol3d"; break;
                                    case TYPE_VOLUME4D: tn="vol4d"; break;
                                    case TYPE_TIMESERIES: tn="ts"; break;
                                    case TYPE_CORRMAP: tn="corrmap"; break;
                                    case TYPE_MASK: tn="mask"; break;
                                    case TYPE_NIL: tn="nil"; break;
                                    case TYPE_AFFINE: tn="affine"; break;
                                }
                                if (vv && vv->type==TYPE_VOLUME3D && vv->nx==1 && vv->ny==1 && vv->nz==1)
                                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                             "  %-20s %s = %.6g", vn, tn, vv->data[0]);
                                else if (vv)
                                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                             "  %-20s %s %dx%dx%d", vn, tn, vv->nx, vv->ny, vv->nz);
                            }
                        } else if (strcmp(sh->symbol, "if") == 0 && sa->list.count >= 3) {
                            GraphNode *cg = bridge_build_with_hook(sa->list.items[1]);
                            int truthy = 0;
                            if (cg && graph_eval(cg) == 0 && cg->result) {
                                Value *cv = cg->result;
                                truthy = cv->is_int ? (cv->idata && cv->idata[0] != 0)
                                                    : (cv->data  && cv->data[0]  != 0.0f);
                            }
                            graph_free(cg);
                            AstNode *branch = truthy ? sa->list.items[2]
                                         : (sa->list.count >= 4 ? sa->list.items[3] : NULL);
                            if (branch) {
                                GraphNode *bg = bridge_build_with_hook(branch);
                                if (bg && graph_eval(bg) == 0 && bg->result) {
                                    char *out = capture_stdout_cb(bg->result);
                                    if (out && g_app->repl_out_count < 200)
                                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128, "  %s", out);
                                    free(out);
                                }
                                graph_free(bg);
                            }
                        } else {
                            GraphNode *gn = bridge_build_with_hook(sa);
                            if (gn && graph_eval(gn) == 0 && gn->result) {
                                char *out = capture_stdout_cb(gn->result);
                                if (out && g_app->repl_out_count < 200)
                                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128, "  %s", out);
                                free(out);
                            }
                            graph_free(gn);
                        }
                    }
                    ast_free(sa);
                }
                fclose(sf);
                if (g_app->repl_out_count < 200)
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  [loaded: %s]", spath);
            }
            ast_free(ast);
            goto check_err;
        }

        /* (save expr "path") — graph_build handles this */
    }

    /* ── generic expression ────────────────────────────────────── */
    {
        /* bare string */
        if (ast->type == AST_STRING) {
            if (g_app->repl_out_count < 200)
                snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                         "  \"%s\"", ast->string);
            ast_free(ast);
            goto check_err;
        }
        GraphNode *gn = bridge_build_with_hook(ast);
        if (gn && graph_eval(gn) == 0 && gn->result &&
            gn->result->type != TYPE_NIL) {
            char *out = capture_stdout_cb(gn->result);
            if (out && g_app->repl_out_count < 200) {
                snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                         "  %s", out);
            }
            free(out);
        }
        graph_free(gn);
    }

    ast_free(ast);

check_err:
    /* errors printed at done: */
    goto done;
done:
    g_in_eval = 0;
    /* ── restore stderr and capture worker errors ─────────── */
    if (saved_stderr >= 0) {
        fflush(stderr);
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        if (err_pipe[0] >= 0) {
            char ebuf[1024];
            ssize_t nr = read(err_pipe[0], ebuf, sizeof(ebuf) - 1);
            close(err_pipe[0]);
            if (nr > 0) {
                ebuf[nr] = '\0';
                while (nr > 0 && (ebuf[nr-1] == '\n' || ebuf[nr-1] == '\r'))
                    ebuf[--nr] = '\0';
                if (nr > 0 && g_app->repl_out_count < 200) {
                    /* prepend to any existing bridge error */
                    if (g_err_len > 0 && g_app->repl_out_count < 200)
                        snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                                 "  %s", g_err_buf);
                    snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                             "  ! %s", ebuf);
                    g_err_len = 0; /* already printed */
                }
            }
        }
    }
    if (g_err_len > 0) {
        if (g_app->repl_out_count < 200)
            snprintf(g_app->repl_output[g_app->repl_out_count++], 128,
                     "  %s", g_err_buf);
        retval = -1;
        g_err_len = 0;
    }
    return retval;
}

/* ── helpers ──────────────────────────────────────────────────── */

/* Build with bridge-ops intercept (graph_build now has the hook) */
static GraphNode *bridge_build_with_hook(AstNode *ast) {
    return graph_build(ast);
}

/* Wrap a subexpression graph node in a (show ...) op */
static GraphNode *make_show_node(GraphNode *sub) {
    GraphNode *g = calloc(1, sizeof(GraphNode));
    g->type = GN_CALL;
    static OpDef show_op_def = {"show", bridge_op_show, 1, 1};
    g->op = &show_op_def;
    g->args = malloc(sizeof(GraphNode *));
    g->args[0] = sub;
    g->arg_count = 1;
    return g;
}

/* Capture val_print for a Value */
static char *capture_stdout_cb(Value *v) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

    int saved = dup(STDOUT_FILENO);
    if (saved < 0) { close(pipefd[0]); close(pipefd[1]); return NULL; }

    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    val_print(v);
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    char buf[512];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    if (n > 0) {
        buf[n] = '\0';
        if (buf[n - 1] == '\n') buf[n - 1] = '\0';
        return strdup(buf);
    }
    return NULL;
}
