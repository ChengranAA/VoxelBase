#include "ops.h"
#include "pool.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <cblas.h>

/* ── Type check helpers ────────────────────────────────────── */
static const char *type_name(VblType t) {
    switch (t) {
        case TYPE_VOLUME4D:  return "volume4d";
        case TYPE_VOLUME3D:  return "volume3d";
        case TYPE_CORRMAP:   return "corrmap";
        case TYPE_TIMESERIES:return "timeseries";
        case TYPE_MASK:      return "mask";
        case TYPE_AFFINE:    return "affine";
        default:             return "?";
    }
}

static int typechk(const char *op, Value *v, VblType expected) {
    if (v->type != expected) {
        fprintf(stderr, "ERROR: %s expects %s, got %s\n",
                op, type_name(expected), type_name(v->type));
        return 0;
    }
    return 1;
}

static int typechk_any(const char *op, Value *v, VblType t1, VblType t2) {
    if (v->type != t1 && v->type != t2) {
        fprintf(stderr, "ERROR: %s expects %s or %s, got %s\n",
                op, type_name(t1), type_name(t2), type_name(v->type));
        return 0;
    }
    return 1;
}

/* Op table — load/save handled in graph.c, not here */
static OpDef g_ops[] = {
    {"mean",      op_mean,      1, 1},
    {"tmean",     op_tmean,     3, 3},
    {"add",       op_add,       2, 2},
    {"sub",       op_sub,       2, 2},
    {"mul",       op_mul,       2, 2},
    {"div",       op_div,       2, 2},
    {"seed",      op_seed,      4, 4},
    {"correlate", op_correlate, 2, 2},
    {"threshold", op_threshold, 2, 2},
    {"mask",      op_mask,      2, 2},
    {"mask-int",  op_mask_int,  3, 3},
    {"vol3d",     op_vol3d,     3, 4},
    {"vol4d",     op_vol4d,     5, 6},
    {"noise",     op_noise,     3, 4},
    {"stdev",     op_stdev,     1, 1},
    {"min",       op_min,       1, 1},
    {"max",       op_max,       1, 1},
    {"sum",       op_sum,       1, 1},
    {"smooth",    op_smooth,    2, 2},
    {"crop",      op_crop,      7, 7},
    {"pad",       op_pad,       7, 7},
    {"bandpass",  op_bandpass,  3, 3},
    {"detrend",   op_detrend,   1, 1},
    {"drift",       op_drift,       1, 2},
    {"tstd",      op_tstd,      1, 1},
    {"eq",        op_eq,        2, 2},
    {"gt",        op_gt,        2, 2},
    {"lt",        op_lt,        2, 2},
    {"and",       op_and,       2, 2},
    {"or",        op_or,        2, 2},
    {"bc+",       op_bcadd,     2, 2},
    {"bc-",       op_bcsub,     2, 2},
    {"bc*",       op_bcmul,     2, 2},
    {"bc/",       op_bcdiv,     2, 2},
    {"slice",     op_slice,     3, 3},
    {"tslice",    op_tslice,    2, 2},
    {"ts-range",  op_ts_range,  3, 3},
    {"copy",      op_copy,      1, 1},
    {"voxel",     op_voxel,     3, 4},
    {"affine",    op_affine,    0, 0},
    {"translate", op_translate, 3, 3},
    {"rotate",    op_rotate,    4, 4},
    {"scale",     op_scale,     3, 4},
    {"apply",     op_apply,     2, 2},
    {NULL, NULL, 0, 0},
};

OpDef *op_find(const char *name) {
    for (int i = 0; g_ops[i].name; i++)
        if (strcmp(g_ops[i].name, name) == 0) return &g_ops[i];
    return NULL;
}

/* ── Element-wise binary op helpers ─────────────────────────── */

static float fn_add(float a, float b) { return a+b; }
static float fn_sub(float a, float b) { return a-b; }
static float fn_mul(float a, float b) { return a*b; }
static float fn_div(float a, float b) { return b!=0.0f ? a/b : 0.0f; }

typedef struct {
    Value *a, *b, *r;
    int64_t n;
    float (*fn)(float, float);
} EwArg;

static void ew_worker(void *arg, int start, int end, int tid) {
    EwArg *ea = (EwArg *)arg;
    (void)tid;
    for (int64_t i = start; i < end; i++)
        ea->r->data[i] = ea->fn(ea->a->data[i], ea->b->data[i]);
}

/* Element-wise binary op — strict shapes */

/* Multiply two 4x4 matrices: r = a * b */
static void mat4_mul(float *r, const float *a, const float *b) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[i*4 + k] * b[k*4 + j];
            r[i*4 + j] = s;
        }
}

static Value *ew_binop(Value *a, Value *b, VblType rt, float (*fn)(float,float)) {
    /* Special case: affine * affine = matrix multiply */
    if (a->type == TYPE_AFFINE && b->type == TYPE_AFFINE) {
        Value *r = val_new_affine();
        mat4_mul(r->data, a->data, b->data);
        snprintf(r->label, sizeof(r->label), "(* affine affine)");
        return r;
    }

    int64_t n = val_voxel_count(a);
    Value *r = (rt == TYPE_VOLUME4D)
        ? val_new_volume4d(a->nx,a->ny,a->nz,a->nt, a->dx,a->dy,a->dz,a->tr)
        : val_new_volume3d(a->nx,a->ny,a->nz, a->dx,a->dy,a->dz);

    if (a->nx != b->nx || a->ny != b->ny || a->nz != b->nz || a->nt != b->nt) {
        fprintf(stderr, "ERROR: shape mismatch (%dx%dx%dx%d vs %dx%dx%dx%d)\n",
                a->nx, a->ny, a->nz, a->nt, b->nx, b->ny, b->nz, b->nt);
        return val_new_nil();
    }

    if (g_pool && n > 10000) {
        EwArg ea = {a, b, r, n, fn};
        pool_submit(g_pool, ew_worker, &ea, (int)n);
    } else {
        for (int64_t i = 0; i < n; i++)
            r->data[i] = fn(a->data[i], b->data[i]);
    }
    return r;
}

Value *op_add(int c, Value **a) { (void)c; Value *r = ew_binop(a[0],a[1], a[0]->type, fn_add); snprintf(r->label, sizeof(r->label), "(+ ...)"); return r; }
Value *op_sub(int c, Value **a) { (void)c; Value *r = ew_binop(a[0],a[1], a[0]->type, fn_sub); snprintf(r->label, sizeof(r->label), "(- ...)"); return r; }
Value *op_mul(int c, Value **a) { (void)c; Value *r = ew_binop(a[0],a[1], a[0]->type, fn_mul); snprintf(r->label, sizeof(r->label), "(* ...)"); return r; }
Value *op_div(int c, Value **a) { (void)c; Value *r = ew_binop(a[0],a[1], a[0]->type, fn_div); snprintf(r->label, sizeof(r->label), "(/ ...)"); return r; }

/* ── Explicit broadcast ops: scalar → volume ────────────────── */
static Value *ew_bc(Value *vol, Value *scalar, float (*fn)(float,float)) {
    int64_t n = val_voxel_count(vol);
    float sv = scalar->data[0];
    /* If vol is a view, mutate in-place so parent sees the change */
    if (!vol->owns_data) {
        for (int64_t i = 0; i < n; i++)
            vol->data[i] = fn(vol->data[i], sv);
        return vol;
    }
    Value *r;
    if (vol->type == TYPE_VOLUME4D)
        r = val_new_volume4d(vol->nx,vol->ny,vol->nz,vol->nt, vol->dx,vol->dy,vol->dz,vol->tr);
    else
        r = val_new_volume3d(vol->nx,vol->ny,vol->nz, vol->dx,vol->dy,vol->dz);
    for (int64_t i = 0; i < n; i++)
        r->data[i] = fn(vol->data[i], sv);
    return r;
}

Value *op_bcadd(int c, Value **a) { (void)c; if (!typechk_any("bc+", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil(); Value *r=ew_bc(a[0],a[1],fn_add); snprintf(r->label,sizeof(r->label),"(bc+ ...)"); return r; }
Value *op_bcsub(int c, Value **a) { (void)c; if (!typechk_any("bc-", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil(); Value *r=ew_bc(a[0],a[1],fn_sub); snprintf(r->label,sizeof(r->label),"(bc- ...)"); return r; }
Value *op_bcmul(int c, Value **a) { (void)c; if (!typechk_any("bc*", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil(); Value *r=ew_bc(a[0],a[1],fn_mul); snprintf(r->label,sizeof(r->label),"(bc* ...)"); return r; }
Value *op_bcdiv(int c, Value **a) { (void)c; if (!typechk_any("bc/", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil(); Value *r=ew_bc(a[0],a[1],fn_div); snprintf(r->label,sizeof(r->label),"(bc/ ...)"); return r; }

/* ── Mean ───────────────────────────────────────────────────── */

typedef struct {
    Value *v;
    Value *r;
    int64_t n3;
    int nt;
} MeanArg;

static void mean_worker(void *arg, int start, int end, int tid) {
    MeanArg *a = (MeanArg *)arg;
    (void)tid;
    for (int64_t i = start; i < end; i++) {
        double sum = 0.0;
        for (int t = 0; t < a->nt; t++)
            sum += (double)a->v->data[t * a->n3 + i];
        a->r->data[i] = (float)(sum / (double)a->nt);
    }
}

Value *op_mean(int c, Value **a) {
    (void)c;
    if (!typechk_any("mean", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx*v->ny*v->nz;
    int nt = v->nt > 0 ? v->nt : 1;
    Value *r = val_new_volume3d(v->nx,v->ny,v->nz, v->dx,v->dy,v->dz);

    if (g_pool && n3 > 10000) {
        MeanArg ma = {v, r, n3, nt};
        pool_submit(g_pool, mean_worker, &ma, (int)n3);
    } else {
        for (int64_t i = 0; i < n3; i++) {
            double sum = 0.0;
            for (int t = 0; t < nt; t++) sum += (double)v->data[t*n3 + i];
            r->data[i] = (float)(sum / (double)nt);
        }
    }
    snprintf(r->label, sizeof(r->label), "(mean %s)", v->label);
    return r;
}

/* ── TMean ──────────────────────────────────────────────────── */

Value *op_tmean(int c, Value **a) {
    (void)c;
    if (!typechk_any("tmean", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int t0 = (int)a[1]->data[0], t1 = (int)a[2]->data[0];
    if (t0 < 0) t0 = 0; if (t1 > v->nt) t1 = v->nt;
    int nt = t1 - t0; if (nt < 1) nt = 1;
    int64_t n3 = (int64_t)v->nx*v->ny*v->nz;
    Value *r = val_new_volume3d(v->nx,v->ny,v->nz, v->dx,v->dy,v->dz);
    for (int64_t i = 0; i < n3; i++) {
        double sum = 0.0;
        for (int t = t0; t < t1; t++) sum += (double)v->data[t*n3 + i];
        r->data[i] = (float)(sum / (double)nt);
    }
    snprintf(r->label, sizeof(r->label), "(tmean %s %d %d)", v->label, t0, t1);
    return r;
}

/* ── Stdev ──────────────────────────────────────────────────── */

Value *op_stdev(int c, Value **a) {
    (void)c;
    if (!typechk_any("stdev", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt > 0 ? v->nt : 1;
    Value *r = val_new_volume3d(v->nx, v->ny, v->nz, v->dx, v->dy, v->dz);
    for (int64_t i = 0; i < n3; i++) {
        double sum = 0.0, sum2 = 0.0;
        for (int t = 0; t < nt; t++) {
            float x = v->data[t * n3 + i];
            sum += x; sum2 += x * x;
        }
        double var = sum2 / nt - (sum / nt) * (sum / nt);
        r->data[i] = (float)(var > 0 ? sqrt(var) : 0);
    }
    snprintf(r->label, sizeof(r->label), "(stdev %s)", v->label);
    return r;
}

/* ── Min ────────────────────────────────────────────────────── */

Value *op_min(int c, Value **a) {
    (void)c;
    if (!typechk_any("min", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt > 0 ? v->nt : 1;
    Value *r = val_new_volume3d(v->nx, v->ny, v->nz, v->dx, v->dy, v->dz);
    for (int64_t i = 0; i < n3; i++) {
        float mn = v->data[i];
        for (int t = 1; t < nt; t++) {
            float x = v->data[t * n3 + i];
            if (x < mn) mn = x;
        }
        r->data[i] = mn;
    }
    snprintf(r->label, sizeof(r->label), "(min %s)", v->label);
    return r;
}

/* ── Max ────────────────────────────────────────────────────── */

Value *op_max(int c, Value **a) {
    (void)c;
    if (!typechk_any("max", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt > 0 ? v->nt : 1;
    Value *r = val_new_volume3d(v->nx, v->ny, v->nz, v->dx, v->dy, v->dz);
    for (int64_t i = 0; i < n3; i++) {
        float mx = v->data[i];
        for (int t = 1; t < nt; t++) {
            float x = v->data[t * n3 + i];
            if (x > mx) mx = x;
        }
        r->data[i] = mx;
    }
    snprintf(r->label, sizeof(r->label), "(max %s)", v->label);
    return r;
}

/* ── Sum ────────────────────────────────────────────────────── */

Value *op_sum(int c, Value **a) {
    (void)c;
    if (!typechk_any("sum", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt > 0 ? v->nt : 1;
    Value *r = val_new_volume3d(v->nx, v->ny, v->nz, v->dx, v->dy, v->dz);
    for (int64_t i = 0; i < n3; i++) {
        double s = 0.0;
        for (int t = 0; t < nt; t++) s += (double)v->data[t * n3 + i];
        r->data[i] = (float)s;
    }
    snprintf(r->label, sizeof(r->label), "(sum %s)", v->label);
    return r;
}

/* ── Seed ───────────────────────────────────────────────────── */

Value *op_seed(int c, Value **a) {
    (void)c;
    if (!typechk("seed", a[0], TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int sx=(int)a[1]->data[0], sy=(int)a[2]->data[0], sz=(int)a[3]->data[0];
    if (sx<0)sx=0; if(sx>=v->nx)sx=v->nx-1;
    if (sy<0)sy=0; if(sy>=v->ny)sy=v->ny-1;
    if (sz<0)sz=0; if(sz>=v->nz)sz=v->nz-1;
    int nt = v->nt;
    int64_t n3 = (int64_t)v->nx*v->ny*v->nz;
    int64_t vo = (int64_t)sx + (int64_t)sy*v->nx + (int64_t)sz*v->nx*v->ny;
    Value *r = val_new_timeseries(nt, v->tr);
    for (int t = 0; t < nt; t++) r->data[t] = v->data[t*n3 + vo];
    snprintf(r->label, sizeof(r->label), "(seed %s %d %d %d)", v->label, sx, sy, sz);
    return r;
}

/* ── Correlate ──────────────────────────────────────────────── */

typedef struct {
    int64_t n3;
    int nt;
    double *sY, *sY2;
    float *dot_f;
    double sumX, stdX;
    float *r_data;
} CorrArg;

static void corr_worker(void *arg, int start, int end, int tid) {
    CorrArg *ca = (CorrArg *)arg;
    (void)tid;
    for (int64_t i = start; i < end; i++) {
        double my = ca->sY[i] / ca->nt;
        double vy = ca->sY2[i] / ca->nt - my * my;
        if (vy < 0) vy = 0;
        double stdY = sqrt(vy);
        double num = (double)ca->dot_f[i] - ca->sumX * my;
        double denom = ca->nt * ca->stdX * stdY;
        if (denom > 1e-10) {
            float rr = (float)(num / denom);
            if (rr > 1) rr = 1;
            if (rr < -1) rr = -1;
            ca->r_data[i] = rr;
        } else {
            ca->r_data[i] = 0;
        }
    }
}

Value *op_correlate(int c, Value **a) {
    (void)c;
    if (!typechk("correlate", a[0], TYPE_VOLUME4D)) return val_new_nil();
    if (!typechk("correlate", a[1], TYPE_TIMESERIES)) return val_new_nil();
    Value *vol=a[0], *ts=a[1];
    int nt = ts->nt;
    int64_t n3 = (int64_t)vol->nx*vol->ny*vol->nz;
    double sumX=0,sumX2=0;
    for (int t=0;t<nt;t++){sumX+=ts->data[t]; sumX2+=ts->data[t]*ts->data[t];}
    double meanX=sumX/nt, varX=sumX2/nt-meanX*meanX;
    if(varX<1e-12)varX=1e-12;
    double stdX=sqrt(varX);
    Value *r = val_new_corrmap(vol->nx,vol->ny,vol->nz, vol->dx,vol->dy,vol->dz);
    double *sY  = calloc(n3, sizeof(double));
    double *sY2 = calloc(n3, sizeof(double));
    float  *dot_f = calloc(n3, sizeof(float));

    /* BLAS sgemv: dot = seed^T * data  (data is nt×n3 row-major → CblasTrans) */
    int n3_int = (int)n3;
    cblas_sgemv(CblasRowMajor, CblasTrans,
                nt, n3_int, 1.0f,
                vol->data, n3_int,
                ts->data, 1, 0.0f, dot_f, 1);

    /* Accumulate sum_Y, sum_Y2 in a separate pass */
    for (int t = 0; t < nt; t++) {
        float *vt = vol->data + t * n3;
        for (int64_t i = 0; i < n3; i++) {
            float y = vt[i];
            sY[i]  += y;
            sY2[i] += y * y;
        }
    }

    /* Compute Pearson r per voxel — parallelized */
    if (g_pool && n3 > 10000) {
        CorrArg ca = {n3, nt, sY, sY2, dot_f, sumX, stdX, r->data};
        pool_submit(g_pool, corr_worker, &ca, (int)n3);
    } else {
        for (int64_t i = 0; i < n3; i++) {
            double my = sY[i] / nt, vy = sY2[i] / nt - my * my;
            if (vy < 0) vy = 0;
            double stdY = sqrt(vy);
            double num = (double)dot_f[i] - sumX * my;
            double denom = nt * stdX * stdY;
            if (denom > 1e-10) {
                float rr = (float)(num / denom);
                if (rr > 1) rr = 1;
                if (rr < -1) rr = -1;
                r->data[i] = rr;
            } else {
                r->data[i] = 0;
            }
        }
    }
    free(sY); free(sY2); free(dot_f);
    snprintf(r->label,sizeof(r->label),"(correlate %s %s)",vol->label,ts->label);
    return r;
}

/* ── Threshold ──────────────────────────────────────────────── */

Value *op_threshold(int c, Value **a) {
    (void)c;
    if (!typechk_any("threshold", a[0], TYPE_VOLUME3D, TYPE_CORRMAP)) return val_new_nil();
    Value *v=a[0]; float th=a[1]->data[0];
    int64_t n=val_voxel_count(v);
    Value *r=val_new_mask(v->nx,v->ny,v->nz,v->dx,v->dy,v->dz);
    for(int64_t i=0;i<n;i++) r->idata[i]=v->data[i]>=th?1:0;
    snprintf(r->label,sizeof(r->label),"(threshold %s %.2f)",v->label,th);
    return r;
}

/* ── Mask ───────────────────────────────────────────────────── */

Value *op_mask(int c, Value **a) {
    (void)c;
    if (!typechk_any("mask", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    if (!typechk("mask", a[1], TYPE_MASK)) return val_new_nil();
    Value *vol=a[0],*msk=a[1];
    int64_t n=val_voxel_count(vol);
    Value *r=val_new_volume3d(vol->nx,vol->ny,vol->nz,vol->dx,vol->dy,vol->dz);
    for(int64_t i=0;i<n;i++) r->data[i]=msk->idata[i]>0?vol->data[i]:0;
    snprintf(r->label,sizeof(r->label),"(mask %s %s)",vol->label,msk->label);
    return r;
}

Value *op_mask_int(int c, Value **a) {
    (void)c;
    int nx = (int)a[0]->data[0];
    int ny = (int)a[1]->data[0];
    int nz = (int)a[2]->data[0];
    return val_new_mask(nx, ny, nz, 1.0, 1.0, 1.0);
}

/* ── Vol3d / Vol4d ──────────────────────────────────────────── */

Value *op_vol3d(int c, Value **a) {
    int nx = (int)a[0]->data[0];
    int ny = (int)a[1]->data[0];
    int nz = (int)a[2]->data[0];
    float fill = (c >= 4) ? a[3]->data[0] : 0.0f;
    Value *r = val_new_volume3d(nx, ny, nz, 1.0, 1.0, 1.0);
    int64_t n = val_voxel_count(r);
    for (int64_t i = 0; i < n; i++) r->data[i] = fill;
    snprintf(r->label, sizeof(r->label), "#<volume3d %dx%dx%d>", nx, ny, nz);
    return r;
}

Value *op_vol4d(int c, Value **a) {
    int nx = (int)a[0]->data[0];
    int ny = (int)a[1]->data[0];
    int nz = (int)a[2]->data[0];
    int nt = (int)a[3]->data[0];
    float tr  = (c >= 6) ? a[4]->data[0] : 2.0f;
    float fill = (c >= 5 && c < 6) ? a[4]->data[0] : 0.0f;
    if (c >= 6) fill = a[5]->data[0];
    Value *r = val_new_volume4d(nx, ny, nz, nt, 1.0, 1.0, 1.0, tr);
    int64_t n = val_voxel_count(r);
    for (int64_t i = 0; i < n; i++) r->data[i] = fill;
    snprintf(r->label, sizeof(r->label), "#<volume4d %dx%dx%dx%d>", nx, ny, nz, nt);
    return r;
}

/* ── Noise ──────────────────────────────────────────────────── */

Value *op_noise(int c, Value **a) {
    int nx = (int)a[0]->data[0];
    int ny = (int)a[1]->data[0];
    int nz = (int)a[2]->data[0];
    float sigma = (c >= 4) ? a[3]->data[0] : 1.0f;
    Value *r = val_new_volume3d(nx, ny, nz, 1.0, 1.0, 1.0);
    int64_t n = val_voxel_count(r);
    for (int64_t i = 0; i < n; i += 2) {
        float u1 = (float)rand() / (float)RAND_MAX;
        float u2 = (float)rand() / (float)RAND_MAX;
        if (u1 < 1e-10f) u1 = 1e-10f;
        float z1 = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265f * u2);
        r->data[i] = z1 * sigma;
        if (i + 1 < n) {
            float z2 = sqrtf(-2.0f * logf(u1)) * sinf(2.0f * 3.14159265f * u2);
            r->data[i + 1] = z2 * sigma;
        }
    }
    snprintf(r->label, sizeof(r->label), "#<volume3d %dx%dx%d noise σ=%.1f>", nx, ny, nz, sigma);
    return r;
}

/* ── Smooth: 3D Gaussian blur (separable: 3-pass 1D convolution) ── */
Value *op_smooth(int c, Value **a) {
    (void)c;
    /* accept timeseries directly */
    if (a[0]->type == TYPE_TIMESERIES) {
        Value *v = a[0];
        if (!v->data || v->nt < 1) return val_new_nil();
        float sigma = a[1]->data[0];
        if (sigma <= 0 || sigma != sigma) { sigma = 1.0f; } /* NaN check */
        int nt = v->nt;
        int r2 = (int)(sigma * 4.0f); if (r2 < 1) r2 = 1;
        if (r2 > nt/2) r2 = nt/2; /* cap kernel to data length */
        int kw = 2 * r2 + 1;
        float *kern = malloc(kw * sizeof(float));
        if (!kern) return val_new_nil();
        float *tmp = malloc(nt * sizeof(float));
        if (!tmp) { free(kern); return val_new_nil(); }
        float ksum = 0;
        for (int i = -r2; i <= r2; i++) {
            float w = expf(-(float)(i*i) / (2.0f * sigma * sigma));
            kern[i + r2] = w; ksum += w;
        }
        for (int i = 0; i < kw; i++) kern[i] /= ksum;
        for (int t = 0; t < nt; t++) {
            float s = 0;
            for (int k = -r2; k <= r2; k++) {
                int tt = t + k;
                if (tt < 0) tt = 0; if (tt >= nt) tt = nt - 1;
                s += v->data[tt] * kern[k + r2];
            }
            tmp[t] = s;
        }
        Value *r = val_new_timeseries(nt, v->tr);
        memcpy(r->data, tmp, nt * sizeof(float));
        free(kern); free(tmp);
        return r;
    }
    if (!typechk_any("smooth", a[0], TYPE_VOLUME3D, TYPE_CORRMAP)) return val_new_nil();
    Value *v = a[0];
    float sigma = a[1]->data[0];
    if (sigma <= 0) { sigma = 1.0f; }
    int nx = v->nx, ny = v->ny, nz = v->nz;
    int64_t n3 = (int64_t)nx * ny * nz;
    Value *r = val_new_volume3d(nx, ny, nz, v->dx, v->dy, v->dz);
    memcpy(r->data, v->data, n3 * sizeof(float));

    /* Build 1D Gaussian kernel */
    int r2 = (int)(sigma * 4.0f);
    if (r2 < 1) r2 = 1;
    int kw = 2 * r2 + 1;
    float *kern = malloc(kw * sizeof(float));
    float ksum = 0;
    for (int i = -r2; i <= r2; i++) {
        float w = expf(-(float)(i*i) / (2.0f * sigma * sigma));
        kern[i + r2] = w; ksum += w;
    }
    for (int i = 0; i < kw; i++) kern[i] /= ksum;

    float *tmp = malloc(n3 * sizeof(float));

    /* Pass 1: X direction */
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                float s = 0;
                for (int k = -r2; k <= r2; k++) {
                    int xx = x + k;
                    if (xx < 0) xx = 0; if (xx >= nx) xx = nx - 1;
                    s += r->data[(int64_t)z*ny*nx + (int64_t)y*nx + xx] * kern[k + r2];
                }
                tmp[(int64_t)z*ny*nx + (int64_t)y*nx + x] = s;
            }
        }
    }
    /* Pass 2: Y direction */
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                float s = 0;
                for (int k = -r2; k <= r2; k++) {
                    int yy = y + k;
                    if (yy < 0) yy = 0; if (yy >= ny) yy = ny - 1;
                    s += tmp[(int64_t)z*ny*nx + (int64_t)yy*nx + x] * kern[k + r2];
                }
                r->data[(int64_t)z*ny*nx + (int64_t)y*nx + x] = s;
            }
        }
    }
    /* Pass 3: Z direction */
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                float s = 0;
                for (int k = -r2; k <= r2; k++) {
                    int zz = z + k;
                    if (zz < 0) zz = 0; if (zz >= nz) zz = nz - 1;
                    s += r->data[(int64_t)zz*ny*nx + (int64_t)y*nx + x] * kern[k + r2];
                }
                tmp[(int64_t)z*ny*nx + (int64_t)y*nx + x] = s;
            }
        }
    }
    memcpy(r->data, tmp, n3 * sizeof(float));
    free(kern); free(tmp);
    snprintf(r->label, sizeof(r->label), "(smooth %s σ=%.1f)", v->label, sigma);
    return r;
}

/* ── Crop: extract subvolume ── */
Value *op_crop(int c, Value **a) {
    (void)c;
    if (!typechk_any("crop", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int x0=(int)a[1]->data[0], y0=(int)a[2]->data[0], z0=(int)a[3]->data[0];
    int x1=(int)a[4]->data[0], y1=(int)a[5]->data[0], z1=(int)a[6]->data[0];
    if (x0 < 0) x0=0; if (x1 > v->nx) x1=v->nx;
    if (y0 < 0) y0=0; if (y1 > v->ny) y1=v->ny;
    if (z0 < 0) z0=0; if (z1 > v->nz) z1=v->nz;
    int nx=x1-x0, ny=y1-y0, nz=z1-z0;
    if (nx<1||ny<1||nz<1) return val_new_volume3d(1,1,1,1,1,1);
    Value *r = val_new_volume3d(nx, ny, nz, v->dx, v->dy, v->dz);
    for (int zz=0; zz<nz; zz++)
        for (int yy=0; yy<ny; yy++)
            memcpy(r->data + (int64_t)zz*ny*nx + (int64_t)yy*nx,
                   v->data + (int64_t)(z0+zz)*v->ny*v->nx + (int64_t)(y0+yy)*v->nx + x0,
                   nx * sizeof(float));
    snprintf(r->label, sizeof(r->label), "(crop %s %d..%d %d..%d %d..%d)", v->label, x0,x1,y0,y1,z0,z1);
    return r;
}

/* ── Pad: extend volume to new dimensions ── */
Value *op_pad(int c, Value **a) {
    (void)c;
    if (!typechk_any("pad", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int nx=(int)a[1]->data[0], ny=(int)a[2]->data[0], nz=(int)a[3]->data[0];
    int x0=(int)a[4]->data[0], y0=(int)a[5]->data[0], z0=(int)a[6]->data[0];
    if (nx<v->nx) nx=v->nx; if (ny<v->ny) ny=v->ny; if (nz<v->nz) nz=v->nz;
    Value *r = val_new_volume3d(nx, ny, nz, v->dx, v->dy, v->dz);
    memset(r->data, 0, (int64_t)nx*ny*nz*sizeof(float));
    for (int z=0; z<v->nz; z++)
        for (int y=0; y<v->ny; y++)
            memcpy(r->data + (int64_t)(z0+z)*ny*nx + (int64_t)(y0+y)*nx + x0,
                   v->data + (int64_t)z*v->ny*v->nx + (int64_t)y*v->nx,
                   v->nx * sizeof(float));
    snprintf(r->label, sizeof(r->label), "(pad %s %dx%dx%d)", v->label, nx, ny, nz);
    return r;
}

/* ── Bandpass: frequency-domain filter (simple: mean subtraction + boxcar) ── */
Value *op_bandpass(int c, Value **a) {
    (void)c;
    /* accept timeseries directly */
    if (a[0]->type == TYPE_TIMESERIES) {
        Value *v = a[0];
        if (!v->data || v->nt < 1) return val_new_nil();
        float lo = a[1]->data[0];
        float hi = a[2]->data[0];
        if (lo <= 0) lo = 0;
        int nt = v->nt;
        int lo_w = (int)(lo > 0 ? v->tr > 0 ? lo / v->tr : lo : 0);
        int hi_w = (int)(hi > 0 ? v->tr > 0 ? hi / v->tr : hi : nt / 4);
        if (lo_w < 0) lo_w = 0;
        if (hi_w < 1) hi_w = 1;
        if (hi_w > nt / 2) hi_w = nt / 2;
        float *tmp = malloc(nt * sizeof(float));
        if (!tmp) return val_new_nil();
        memcpy(tmp, v->data, nt * sizeof(float));
        if (lo_w > 0) {
            float *run = calloc(nt, sizeof(float));
            if (!run) { free(tmp); return val_new_nil(); }
            for (int t = 0; t < nt; t++) {
                int w0 = t - lo_w; if (w0 < 0) w0 = 0;
                int w1 = t + lo_w; if (w1 >= nt) w1 = nt - 1;
                float sum = 0; int cnt = 0;
                for (int j = w0; j <= w1; j++) { sum += v->data[j]; cnt++; }
                run[t] = sum / cnt;
            }
            for (int t = 0; t < nt; t++) tmp[t] -= run[t];
            free(run);
        }
        if (hi_w > 0 && hi_w < nt / 2) {
            float *lp = calloc(nt, sizeof(float));
            if (!lp) { free(tmp); return val_new_nil(); }
            for (int t = 0; t < nt; t++) {
                int w0 = t - hi_w; if (w0 < 0) w0 = 0;
                int w1 = t + hi_w; if (w1 >= nt) w1 = nt - 1;
                float sum = 0; int cnt = 0;
                for (int j = w0; j <= w1; j++) { sum += tmp[j]; cnt++; }
                lp[t] = sum / cnt;
            }
            memcpy(tmp, lp, nt * sizeof(float));
            free(lp);
        }
        Value *r = val_new_timeseries(nt, v->tr);
        memcpy(r->data, tmp, nt * sizeof(float));
        free(tmp);
        return r;
    }
    if (!typechk_any("bandpass", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    float lo = a[1]->data[0];
    float hi = a[2]->data[0];
    if (lo <= 0) lo = 0;
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt;
    Value *r = val_new_volume4d(v->nx, v->ny, v->nz, nt, v->dx, v->dy, v->dz, v->tr);

    /* Simple approach: detrend + moving-average bandpass */
    for (int64_t i = 0; i < n3; i++) {
        /* Extract timeseries */
        double *ts = malloc(nt * sizeof(double));
        for (int t = 0; t < nt; t++) ts[t] = v->data[t * n3 + i];

        /* Subtract linear trend */
        double sx=0, sy=0, sxx=0, sxy=0;
        for (int t=0; t<nt; t++) { double tx=t; sx+=tx; sy+=ts[t]; sxx+=tx*tx; sxy+=tx*ts[t]; }
        double a_slope = (nt*sxy - sx*sy) / (nt*sxx - sx*sx + 1e-10);
        double a_int = (sy - a_slope*sx) / nt;
        for (int t=0; t<nt; t++) ts[t] -= (a_slope*t + a_int);

        /* Low-pass: moving average with window = lo */
        int win = (int)lo;
        if (win < 1) win = 1;
        if (win >= nt) win = nt - 1;
        double *lp = malloc(nt * sizeof(double));
        for (int t=0; t<nt; t++) {
            int t0 = t - win/2, t1 = t0 + win;
            if (t0 < 0) t0 = 0; if (t1 > nt) { t1 = nt; t0 = nt - win; }
            double s = 0; for (int u=t0; u<t1; u++) s += ts[u];
            lp[t] = s / (t1 - t0);
        }

        /* High-pass: subtract low-pass */
        for (int t=0; t<nt; t++) r->data[t * n3 + i] = (float)(ts[t] - (hi > 0 ? lp[t] : 0));
        free(ts); free(lp);
    }
    snprintf(r->label, sizeof(r->label), "(bandpass %s lo=%.1f hi=%.1f)", v->label, lo, hi);
    return r;
}

/* ── Detrend: remove linear trend per voxel ── */
Value *op_detrend(int c, Value **a) {
    (void)c;
    /* accept timeseries directly */
    if (a[0]->type == TYPE_TIMESERIES) {
        Value *v = a[0];
        if (!v->data || v->nt < 1) return val_new_nil();
        int nt = v->nt;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int t = 0; t < nt; t++) {
            double tx = t;
            float y = v->data[t];
            sx += tx; sy += y; sxx += tx * tx; sxy += tx * y;
        }
        double slope = (nt * sxy - sx * sy) / (nt * sxx - sx * sx + 1e-10);
        double intercept = (sy - slope * sx) / nt;
        Value *r = val_new_timeseries(nt, v->tr);
        for (int t = 0; t < nt; t++)
            r->data[t] = v->data[t] - (float)(slope * t + intercept);
        return r;
    }
    if (!typechk_any("detrend", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt;
    Value *r = val_new_volume4d(v->nx, v->ny, v->nz, nt, v->dx, v->dy, v->dz, v->tr);
    for (int64_t i = 0; i < n3; i++) {
        double sx=0, sy=0, sxx=0, sxy=0;
        for (int t=0; t<nt; t++) { double tx=t; float y=v->data[t*n3+i]; sx+=tx; sy+=y; sxx+=tx*tx; sxy+=tx*y; }
        double slope = (nt*sxy - sx*sy) / (nt*sxx - sx*sx + 1e-10);
        double inter = (sy - slope*sx) / nt;
        for (int t=0; t<nt; t++) r->data[t*n3+i] = (float)(v->data[t*n3+i] - (slope*t + inter));
    }
    snprintf(r->label, sizeof(r->label), "(detrend %s)", v->label);
    return r;
}

/* ── Drift: frame-wise displacement ──────────────────────────── */
/* RMS diff between consecutive frames, optional downsampling stride */
Value *op_drift(int c, Value **a) {
    if (!typechk("drift", a[0], TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int factor = (c >= 2) ? (int)a[1]->data[0] : 1;
    if (factor < 1) factor = 1;

    int nt = v->nt;
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int64_t n_eff = (n3 + factor - 1) / factor;
    Value *r = val_new_timeseries(nt, v->tr);
    float *f0 = malloc((size_t)n3 * sizeof(float));
    for (int64_t i = 0; i < n3; i++) f0[i] = v->data[i];
    for (int t = 0; t < nt; t++) {
        double sum = 0.0;
        int64_t count = 0;
        for (int64_t i = 0; i < n3; i += factor) {
            if (fabsf(f0[i]) < 1e-6f) continue;
            sum += (double)fabsf(v->data[t * n3 + i] - f0[i]);
            count++;
        }
        r->data[t] = count > 0 ? (float)(sum / (double)count) : 0.0f;
    }
    free(f0);
    snprintf(r->label, sizeof(r->label), "(drift %s)", v->label);
    return r;
}

/* ── Tstd: temporal standard deviation ── */
Value *op_tstd(int c, Value **a) {
    (void)c;
    if (!typechk_any("tstd", a[0], TYPE_VOLUME4D, TYPE_VOLUME3D)) return val_new_nil();
    Value *v = a[0];
    int64_t n3 = (int64_t)v->nx * v->ny * v->nz;
    int nt = v->nt;
    Value *r = val_new_volume3d(v->nx, v->ny, v->nz, v->dx, v->dy, v->dz);
    for (int64_t i = 0; i < n3; i++) {
        double s=0, s2=0;
        for (int t=0; t<nt; t++) { float x=v->data[t*n3+i]; s+=x; s2+=x*x; }
        double var = s2/nt - (s/nt)*(s/nt);
        r->data[i] = (float)(var > 0 ? sqrt(var) : 0);
    }
    snprintf(r->label, sizeof(r->label), "(tstd %s)", v->label);
    return r;
}

/* ── Element-wise comparison ops (scalar broadcast) ── */
static Value *ew_cmp(Value *a, Value *b, int (*fn)(float,float)) {
    int64_t n = val_voxel_count(a);
    int64_t bn = val_voxel_count(b);
    int broadcast = (bn == 1);
    Value *r = val_new_mask(a->nx, a->ny, a->nz, a->dx, a->dy, a->dz);
    for (int64_t i = 0; i < n; i++) {
        float va = a->is_int ? (float)a->idata[i] : a->data[i];
        float vb = broadcast ? (b->is_int ? (float)b->idata[0] : b->data[0])
                            : (b->is_int ? (float)b->idata[i] : b->data[i]);
        r->idata[i] = fn(va, vb) ? 1 : 0;
    }
    return r;
}
static int cmp_eq(float a, float b) { return a == b; }
static int cmp_gt(float a, float b) { return a > b; }
static int cmp_lt(float a, float b) { return a < b; }
static int cmp_and(float a, float b) { return (a > 0) && (b > 0); }
static int cmp_or(float a, float b)  { return (a > 0) || (b > 0); }

Value *op_eq(int c, Value **a)  { (void)c; Value *r=ew_cmp(a[0],a[1],cmp_eq);  snprintf(r->label,sizeof(r->label),"(==)"); return r; }
Value *op_gt(int c, Value **a)  { (void)c; Value *r=ew_cmp(a[0],a[1],cmp_gt);  snprintf(r->label,sizeof(r->label),"(>)"); return r; }
Value *op_lt(int c, Value **a)  { (void)c; Value *r=ew_cmp(a[0],a[1],cmp_lt);  snprintf(r->label,sizeof(r->label),"(<)"); return r; }
Value *op_and(int c, Value **a) { (void)c; Value *r=ew_cmp(a[0],a[1],cmp_and); snprintf(r->label,sizeof(r->label),"(&&)"); return r; }
Value *op_or(int c, Value **a)  { (void)c; Value *r=ew_cmp(a[0],a[1],cmp_or);  snprintf(r->label,sizeof(r->label),"(||)"); return r; }

/* ── Slice: extract 2D slice (view or copy) from a 3D/4D volume ── */
/* (slice vol axis z) — axis: 0=axial(xy), 1=sagittal(yz), 2=coronal(xz) */
Value *op_slice(int c, Value **a) {
    (void)c;
    if (!typechk_any("slice", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int axis = (int)a[1]->data[0];
    int coord = (int)a[2]->data[0];
    return val_view_slice(v, axis, coord);
}

/* ── Tslice: extract timepoint (3D view from 4D) ── */
/* (tslice vol t) */
Value *op_tslice(int c, Value **a) {
    (void)c;
    if (!typechk("tslice", a[0], TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int t = (int)a[1]->data[0];
    return val_view_tslice(v, t);
}

/* (ts-range ts t0 t1) */
Value *op_ts_range(int c, Value **a) {
    (void)c;
    if (a[0]->type != TYPE_TIMESERIES) {
        fprintf(stderr, "ERROR: ts-range expects timeseries\n");
        return val_new_nil();
    }
    Value *v = a[0];
    if (!v->data || v->nt < 1) return val_new_nil();
    int t0 = (int)a[1]->data[0];
    int t1 = (int)a[2]->data[0];
    if (t0 < 0) t0 = 0;
    if (t1 >= v->nt) t1 = v->nt - 1;
    if (t0 > t1) { int tmp = t0; t0 = t1; t1 = tmp; }
    int n = t1 - t0 + 1;
    Value *r = val_new_timeseries(n, v->tr);
    memcpy(r->data, v->data + t0, (size_t)n * sizeof(float));
    return r;
}

/* ── Copy: deep copy a value ── */
/* (copy v) */
Value *op_copy(int c, Value **a) {
    (void)c;
    return val_copy(a[0]);
}

/* ── Voxel: read a single voxel value ── */
/* (voxel vol x y z [t]) */
Value *op_voxel(int c, Value **a) {
    (void)c;
    if (!typechk_any("voxel", a[0], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    Value *v = a[0];
    int x = (int)a[1]->data[0];
    int y = (int)a[2]->data[0];
    int z = (int)a[3]->data[0];
    if (x < 0) x = 0; if (x >= v->nx) x = v->nx - 1;
    if (y < 0) y = 0; if (y >= v->ny) y = v->ny - 1;
    if (z < 0) z = 0; if (z >= v->nz) z = v->nz - 1;
    int64_t off = (int64_t)z * v->ny * v->nx + (int64_t)y * v->nx + x;
    float val = v->data[off];
    Value *r = val_new_volume3d(1, 1, 1, 1, 1, 1);
    r->data[0] = val;
    snprintf(r->label, sizeof(r->label), "(voxel %s %d,%d,%d)=%.3g", v->label, x, y, z, val);
    return r;
}

/* ── Affine transform ── */

Value *op_affine(int c, Value **a) {
    (void)c; (void)a;
    return val_new_affine();
}

Value *op_translate(int c, Value **a) {
    (void)c;
    float tx = a[0]->data[0], ty = a[1]->data[0], tz = a[2]->data[0];
    Value *r = val_new_affine();
    r->data[3] = tx; r->data[7] = ty; r->data[11] = tz;
    snprintf(r->label, sizeof(r->label), "#<affine translate(%.1f,%.1f,%.1f)>", tx, ty, tz);
    return r;
}

Value *op_rotate(int c, Value **a) {
    (void)c;
    float angle = a[0]->data[0] * 3.1415926535f / 180.0f;
    int axis = (int)a[1]->data[0]; /* 0=x, 1=y, 2=z */
    float s = sinf(angle), cs = cosf(angle);
    Value *r = val_new_affine();
    if (axis == 0) {
        r->data[5] = cs; r->data[6] = -s;
        r->data[9] = s; r->data[10] = cs;
    } else if (axis == 1) {
        r->data[0] = cs; r->data[2] = s;
        r->data[8] = -s; r->data[10] = cs;
    } else {
        r->data[0] = cs; r->data[1] = -s;
        r->data[4] = s; r->data[5] = cs;
    }
    snprintf(r->label, sizeof(r->label), "#<affine rotate(%.1f°, axis=%d)>", angle*180/3.1415926535, axis);
    return r;
}

Value *op_scale(int c, Value **a) {
    (void)c;
    float sx = a[0]->data[0], sy = a[1]->data[0], sz = a[2]->data[0];
    Value *r = val_new_affine();
    r->data[0] = sx; r->data[5] = sy; r->data[10] = sz;
    snprintf(r->label, sizeof(r->label), "#<affine scale(%.1f,%.1f,%.1f)>", sx, sy, sz);
    return r;
}

/* ── Apply affine to volume ── */
Value *op_apply(int c, Value **a) {
    (void)c;
    if (!typechk("apply", a[0], TYPE_AFFINE)) return val_new_nil();
    if (!typechk_any("apply", a[1], TYPE_VOLUME3D, TYPE_VOLUME4D)) return val_new_nil();
    Value *aff = a[0];
    Value *vol = a[1];

    Value *r;
    if (vol->type == TYPE_VOLUME4D)
        r = val_new_volume4d(vol->nx, vol->ny, vol->nz, vol->nt, vol->dx, vol->dy, vol->dz, vol->tr);
    else
        r = val_new_volume3d(vol->nx, vol->ny, vol->nz, vol->dx, vol->dy, vol->dz);

    int64_t n3 = (int64_t)vol->nx * vol->ny * vol->nz;
    int nt = vol->nt > 0 ? vol->nt : 1;

    /* Extract only the scale+translate part for simplicity */
    float sx = aff->data[0], sy = aff->data[5], sz = aff->data[10];
    float tx = aff->data[3], ty = aff->data[7], tz = aff->data[11];

    for (int t = 0; t < nt; t++) {
        float *src = vol->data + t * n3;
        float *dst = r->data + t * n3;
        for (int z = 0; z < vol->nz; z++) {
            for (int y = 0; y < vol->ny; y++) {
                for (int x = 0; x < vol->nx; x++) {
                    /* Apply inverse: find where this voxel came from */
                    float sx_v = (x - tx) / (sx + 1e-10f);
                    float sy_v = (y - ty) / (sy + 1e-10f);
                    float sz_v = (z - tz) / (sz + 1e-10f);
                    int ix = (int)(sx_v + 0.5f);
                    int iy = (int)(sy_v + 0.5f);
                    int iz = (int)(sz_v + 0.5f);
                    if (ix >= 0 && ix < vol->nx && iy >= 0 && iy < vol->ny && iz >= 0 && iz < vol->nz)
                        dst[z*n3/vol->nt + y*vol->nx + x] = src[iz*vol->ny*vol->nx + iy*vol->nx + ix];
                }
            }
        }
    }
    snprintf(r->label, sizeof(r->label), "(apply %s %s)", aff->label, vol->label);
    return r;
}
