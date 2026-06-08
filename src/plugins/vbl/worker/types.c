/* ── NIfTI implementation ────────────────────────────────────── */
/*
 *  When building as part of VoxelBase (VXB_INTEGRATED defined),
 *  main.c provides the NIfTI implementation.  Otherwise, include it
 *  here for standalone vbl-worker builds.
 */
#ifndef VXB_INTEGRATED
#define NIFTI_BASE_IMPLEMENTATION
#define NIFTI_ZNZ_IMPLEMENTATION
#define NIFTI_HEADER_IMPLEMENTATION
#define NIFTI_IMAGE_IMPLEMENTATION
#endif

#include "types.h"
#include "nifti/nifti_image.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- internal allocator ---- */
static Value *val_alloc(VblType type) {
    Value *v = calloc(1, sizeof(Value));
    v->type = type;
    v->owns_data = 1;
    return v;
}

/* ---- constructors (zero-filled data) ---- */

Value *val_new_volume4d(int nx, int ny, int nz, int nt,
                        double dx, double dy, double dz, double tr) {
    Value *v = val_alloc(TYPE_VOLUME4D);
    v->nx = nx;  v->ny = ny;  v->nz = nz;  v->nt = nt;
    v->dx = dx;  v->dy = dy;  v->dz = dz;  v->tr = tr;
    v->data_len = (int64_t)nx * ny * nz * nt;
    v->data = calloc((size_t)v->data_len, sizeof(float));
    snprintf(v->label, sizeof(v->label),
             "#<volume4d %dx%dx%dx%d>", nx, ny, nz, nt);
    return v;
}

Value *val_new_volume3d(int nx, int ny, int nz,
                        double dx, double dy, double dz) {
    Value *v = val_alloc(TYPE_VOLUME3D);
    v->nx = nx;  v->ny = ny;  v->nz = nz;  v->nt = 1;
    v->dx = dx;  v->dy = dy;  v->dz = dz;
    v->data_len = (int64_t)nx * ny * nz;
    v->data = calloc((size_t)v->data_len, sizeof(float));
    snprintf(v->label, sizeof(v->label),
             "#<volume3d %dx%dx%d>", nx, ny, nz);
    return v;
}

Value *val_new_timeseries(int len, double tr) {
    if (len < 1) len = 1;
    Value *v = val_alloc(TYPE_TIMESERIES);
    v->nt = len;  v->tr = tr;  v->data_len = len;
    v->data = calloc((size_t)len, sizeof(float));
    if (!v->data) { free(v); return val_new_nil(); }
    snprintf(v->label, sizeof(v->label), "#<timeseries %d>", len);
    return v;
}

Value *val_new_corrmap(int nx, int ny, int nz,
                       double dx, double dy, double dz) {
    Value *v = val_alloc(TYPE_CORRMAP);
    v->nx = nx;  v->ny = ny;  v->nz = nz;
    v->dx = dx;  v->dy = dy;  v->dz = dz;
    v->data_len = (int64_t)nx * ny * nz;
    v->data = calloc((size_t)v->data_len, sizeof(float));
    snprintf(v->label, sizeof(v->label),
             "#<corrmap %dx%dx%d>", nx, ny, nz);
    return v;
}

Value *val_new_mask(int nx, int ny, int nz,
                    double dx, double dy, double dz) {
    Value *v = val_alloc(TYPE_MASK);
    v->nx = nx; v->ny = ny; v->nz = nz; v->nt = 1;
    v->dx = dx; v->dy = dy; v->dz = dz;
    v->is_int = 1;
    v->idata = calloc((size_t)nx * ny * nz, sizeof(int16_t));
    snprintf(v->label, sizeof(v->label), "#<mask %dx%dx%d>", nx, ny, nz);
    return v;
}

Value *val_new_mask_int(int nx, int ny, int nz, double dx, double dy, double dz) {
    Value *v = val_new_mask(nx, ny, nz, dx, dy, dz);
    v->is_int = 1;
    v->idata = calloc((int64_t)nx * ny * nz, sizeof(int16_t));
    snprintf(v->label, sizeof(v->label), "#<mask-int %dx%dx%d>", nx, ny, nz);
    return v;
}

Value *val_new_nil(void) {
    Value *v = val_alloc(TYPE_NIL);
    snprintf(v->label, sizeof(v->label), "#<nil>");
    return v;
}

Value *val_new_affine(void) {
    Value *v = val_alloc(TYPE_AFFINE);
    v->data_len = 16;
    v->data = calloc(16, sizeof(float));
    v->data[0] = 1; v->data[5] = 1; v->data[10] = 1; v->data[15] = 1;
    v->nx = 4; v->ny = 4; v->nz = 1; v->nt = 1;
    snprintf(v->label, sizeof(v->label), "#<affine identity>");
    return v;
}

/* ---- constructors with data copy ---- */

Value *val_new_volume4d_from_buf(int nx, int ny, int nz, int nt,
                                 double dx, double dy, double dz, double tr,
                                 const float *data) {
    Value *v = val_new_volume4d(nx, ny, nz, nt, dx, dy, dz, tr);
    memcpy(v->data, data, (size_t)v->data_len * sizeof(float));
    return v;
}

Value *val_new_volume3d_from_buf(int nx, int ny, int nz,
                                 double dx, double dy, double dz,
                                 const float *data) {
    Value *v = val_new_volume3d(nx, ny, nz, dx, dy, dz);
    memcpy(v->data, data, (size_t)v->data_len * sizeof(float));
    return v;
}

/* ---- destructor ---- */

void val_free(Value *v) {
    if (!v) return;
    if (v->owns_data) {
        free(v->data);
        free(v->idata);
    }
    free(v);
}

/* ---- utilities ---- */

int64_t val_voxel_count(const Value *v) {
    if (!v) return 0;
    return (int64_t)v->nx * v->ny * v->nz * (v->nt > 0 ? v->nt : 1);
}

void val_print(const Value *v) {
    if (!v) { printf("#<nil>\n"); return; }
    switch (v->type) {
        case TYPE_VOLUME3D:
        case TYPE_VOLUME4D:
        case TYPE_CORRMAP:
        case TYPE_MASK: {
            int64_t n = val_voxel_count(v);
            if (n == 0) { printf("#<empty>\n"); return; }
            /* Integer mask printing */
            if (v->is_int) {
                int16_t *d = v->idata;
                int min = d[0], max = d[0];
                for (int64_t i = 0; i < n; i++) {
                    if (d[i] < min) min = d[i];
                    if (d[i] > max) max = d[i];
                }
                printf("#<mask-int %dx%dx%d labels=%d..%d>\n", v->nx, v->ny, v->nz, min, max);
                return;
            }
            /* Scalar shortcut: 1×1×1 volumes print as plain value */
            if (v->nx == 1 && v->ny == 1 && v->nz == 1 && v->nt <= 1) {
                printf("%.6g\n", v->data[0]);
                return;
            }
            double min = v->data[0], max = v->data[0], sum = 0.0;
            for (int64_t i = 0; i < n; i++) {
                float x = v->data[i];
                if (x < min) min = x;
                if (x > max) max = x;
                sum += (double)x;
            }
            double mean = sum / (double)n;
            const char *tname = "?";
            if (v->type == TYPE_VOLUME4D) tname = "volume4d";
            else if (v->type == TYPE_VOLUME3D) tname = "volume3d";
            else if (v->type == TYPE_CORRMAP) tname = "corrmap";
            else tname = "mask";
            if (v->type == TYPE_VOLUME4D)
                printf("#<%s %dx%dx%dx%d [%.3g..%.3g μ=%.3g]>\n",
                       tname, v->nx, v->ny, v->nz, v->nt, min, max, mean);
            else
                printf("#<%s %dx%dx%d [%.3g..%.3g μ=%.3g]>\n",
                       tname, v->nx, v->ny, v->nz, min, max, mean);
            break;
        }
        case TYPE_TIMESERIES: {
            double min = v->data[0], max = v->data[0], sum = 0.0;
            for (int t = 0; t < v->nt; t++) {
                float x = v->data[t];
                if (x < min) min = x; if (x > max) max = x;
                sum += (double)x;
            }
            printf("#<timeseries %d tr=%.1fs [%.3g..%.3g μ=%.3g]>\n",
                   v->nt, v->tr, min, max, sum / v->nt);
            break;
        }
        case TYPE_NIL: printf("#<nil>\n"); break;
        case TYPE_AFFINE: {
            printf("#<affine\n");
            for (int r = 0; r < 4; r++) {
                printf("  [");
                for (int c = 0; c < 4; c++)
                    printf(" % .3g", v->data[r*4 + c]);
                printf(" ]\n");
            }
            printf(">\n");
            break;
        }
    }
}

/* ---- view / copy ---- */

Value *val_view_slice(Value *parent, int axis, int coord) {
    Value *v = calloc(1, sizeof(Value));
    v->owns_data = 0;
    v->parent = parent;
    if (axis == 0) { /* axial — contiguous view */
        if (coord < 0) coord = 0; if (coord >= parent->nz) coord = parent->nz - 1;
        v->type = TYPE_VOLUME3D;
        v->nx = parent->nx; v->ny = parent->ny; v->nz = 1; v->nt = 1;
        v->dx = parent->dx; v->dy = parent->dy; v->dz = parent->dz;
        v->data_len = (int64_t)parent->nx * parent->ny;
        v->data = parent->data + (int64_t)coord * parent->nx * parent->ny;
        snprintf(v->label, sizeof(v->label), "<view axial z=%d>", coord);
    } else if (axis == 1) { /* sagittal — non-contiguous, must copy */
        v->owns_data = 1;
        v->parent = NULL;
        v->type = TYPE_VOLUME3D;
        v->nx = parent->ny; v->ny = parent->nz; v->nz = 1; v->nt = 1;
        v->data_len = (int64_t)parent->ny * parent->nz;
        v->data = calloc((size_t)v->data_len, sizeof(float));
        for (int z = 0; z < parent->nz; z++)
            for (int y = 0; y < parent->ny; y++)
                v->data[z * parent->ny + y] = parent->data[(int64_t)z * parent->ny * parent->nx + (int64_t)y * parent->nx + coord];
        snprintf(v->label, sizeof(v->label), "<copy sagittal x=%d>", coord);
    } else { /* coronal — non-contiguous, must copy */
        v->owns_data = 1;
        v->parent = NULL;
        v->type = TYPE_VOLUME3D;
        v->nx = parent->nx; v->ny = parent->nz; v->nz = 1; v->nt = 1;
        v->data_len = (int64_t)parent->nx * parent->nz;
        v->data = calloc((size_t)v->data_len, sizeof(float));
        for (int z = 0; z < parent->nz; z++)
            for (int x = 0; x < parent->nx; x++)
                v->data[z * parent->nx + x] = parent->data[(int64_t)z * parent->ny * parent->nx + (int64_t)coord * parent->nx + x];
        snprintf(v->label, sizeof(v->label), "<copy coronal y=%d>", coord);
    }
    return v;
}

Value *val_view_tslice(Value *parent, int t) {
    if (t < 0) t = 0; if (t >= parent->nt) t = parent->nt - 1;
    Value *v = calloc(1, sizeof(Value));
    v->owns_data = 0;
    v->parent = parent;
    v->type = TYPE_VOLUME3D;
    v->nx = parent->nx; v->ny = parent->ny; v->nz = parent->nz; v->nt = 1;
    v->dx = parent->dx; v->dy = parent->dy; v->dz = parent->dz;
    v->data_len = (int64_t)parent->nx * parent->ny * parent->nz;
    v->data = parent->data + t * v->data_len;
    snprintf(v->label, sizeof(v->label), "<view t=%d>", t);
    return v;
}

Value *val_copy(const Value *v) {
    if (!v) return NULL;
    Value *r = calloc(1, sizeof(Value));
    *r = *v;
    r->owns_data = 1;
    r->parent = NULL;
    if (v->is_int) {
        int64_t nc = (int64_t)v->nx * v->ny * v->nz;
        r->idata = malloc((size_t)nc * sizeof(int16_t));
        memcpy(r->idata, v->idata, (size_t)nc * sizeof(int16_t));
    } else {
        r->data = malloc((size_t)r->data_len * sizeof(float));
        memcpy(r->data, v->data, (size_t)r->data_len * sizeof(float));
    }
    snprintf(r->label, sizeof(r->label), "<copy of %s>", v->label);
    return r;
}

/* ---- NIfTI I/O ---- */

Value *val_load_nifti(const char *path) {
    /* load image with data */
    nifti_image *nim = nifti_image_load(path, 1);
    if (!nim) {
        fprintf(stderr, "ERROR: cannot open '%s'\n", path);
        return NULL;
    }

    /* ensure float32 data */
    nifti_image *nim_f32 = NULL;
    float *data;
    if (nim->datatype != NIFTI_TYPE_FLOAT32) {
        nim_f32 = nifti_image_copy_to_datatype(nim, NIFTI_TYPE_FLOAT32);
        data = (float *)nim_f32->data;
    } else {
        data = (float *)nim->data;
    }

    int nt = nim->nt > 0 ? (int)nim->nt : 1;
    Value *v;
    if (nt > 1) {
        v = val_new_volume4d((int)nim->nx, (int)nim->ny, (int)nim->nz, nt,
                             nim->dx, nim->dy, nim->dz, nim->dt);
    } else {
        v = val_new_volume3d((int)nim->nx, (int)nim->ny, (int)nim->nz,
                             nim->dx, nim->dy, nim->dz);
    }

    /* copy voxel data */
    int64_t nvox = (int64_t)nim->nx * nim->ny * nim->nz * nt;
    if (nim->datatype == NIFTI_TYPE_INT16 || nim->datatype == NIFTI_TYPE_UINT8 ||
        nim->datatype == NIFTI_TYPE_INT8) {
        /* Integer mask — store as int16 */
        v->is_int = 1;
        v->idata = calloc((size_t)nvox, sizeof(int16_t));
        int16_t *src_i = (int16_t *)(nim_f32 ? nim_f32->data : nim->data);
        for (int64_t i = 0; i < nvox; i++)
            v->idata[i] = src_i[i];
        snprintf(v->label, sizeof(v->label), "#<mask-int %dx%dx%d>", v->nx, v->ny, v->nz);
    } else {
        memcpy(v->data, data, (size_t)nvox * sizeof(float));
    }

    /* label from path */
    snprintf(v->label, sizeof(v->label), "%s", path);

    if (nim_f32) nifti_image_free(nim_f32);
    nifti_image_free(nim);
    return v;
}

int val_save_nifti(const Value *v, const char *path) {
    if (!v || !v->data) return -1;

    int nt = v->nt > 0 ? v->nt : 1;
    nifti_image *nim = nifti_image_new_simple(
        v->nx, v->ny, v->nz, nt, NIFTI_TYPE_FLOAT32);
    if (!nim) return -1;

    nim->dx = v->dx;
    nim->dy = v->dy;
    nim->dz = v->dz;
    nim->dt = v->tr;

    int64_t nvox = val_voxel_count(v);
    nim->data = malloc((size_t)nvox * sizeof(float));
    if (!nim->data) { nifti_image_free(nim); return -1; }
    memcpy(nim->data, v->data, (size_t)nvox * sizeof(float));

    int ret = nifti_image_write(nim, path);
    nifti_image_free(nim);
    return ret;
}
