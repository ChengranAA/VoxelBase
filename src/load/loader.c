#include <float.h>
#include <stdio.h>
#include "app.h"
#include "cli/cli.h"

#define NBINS 4096

static float *convert_volume_float32(nifti_image *nim, ImageSlot *slot) {
    int64_t nvox_total = nim->nvox;
    int64_t nvox_t0    = (int64_t)slot->nx * slot->ny * slot->nz;
    int     src_dtype  = nim->datatype;
    double  slope      = nim->scl_slope;
    double  inter      = nim->scl_inter;

    float *dst = (float *)malloc((size_t)nvox_total * sizeof(float));
    if (!dst) return NULL;

    double lo = DBL_MAX, hi = -DBL_MAX;

    if (src_dtype == NIFTI_TYPE_INT16 && slope == 1.0 && inter == 0.0) {
        const short *s = (const short *)nim->data;
        for (int64_t i = 0; i < nvox_total; i++) {
            float v = (float)s[i];
            dst[i] = v;
            if (i < nvox_t0) { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
    } else if (src_dtype == NIFTI_TYPE_INT16) {
        const short *s = (const short *)nim->data;
        for (int64_t i = 0; i < nvox_total; i++) {
            float v = (float)((double)s[i] * slope + inter);
            dst[i] = v;
            if (i < nvox_t0) { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
    } else if (src_dtype == NIFTI_TYPE_UINT8 && slope == 1.0 && inter == 0.0) {
        const unsigned char *s = (const unsigned char *)nim->data;
        for (int64_t i = 0; i < nvox_total; i++) {
            float v = (float)s[i];
            dst[i] = v;
            if (i < nvox_t0) { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
    } else if (src_dtype == NIFTI_TYPE_FLOAT32 && slope == 1.0 && inter == 0.0) {
        memcpy(dst, nim->data, (size_t)nvox_total * sizeof(float));
        for (int64_t i = 0; i < nvox_t0; i++) {
            float v = dst[i];
            if (v < lo) lo = v; if (v > hi) hi = v;
        }
    } else if (src_dtype == NIFTI_TYPE_FLOAT32) {
        const float *s = (const float *)nim->data;
        for (int64_t i = 0; i < nvox_total; i++) {
            float v = (float)((double)s[i] * slope + inter);
            dst[i] = v;
            if (i < nvox_t0) { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
    } else {
        /* generic fallback */
        for (int64_t i = 0; i < nvox_total; i++) {
            float v = (float)nifti_image_get_voxel(nim, i % slot->nx,
                (i / slot->nx) % slot->ny,
                (i / ((int64_t)slot->nx * slot->ny)) % slot->nz, 0);
            dst[i] = v;
            if (i < nvox_t0) { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
    }

    /* 2nd-98th percentile autorange */
    if (nvox_t0 >= 4096 && lo < hi) {
        int hist[NBINS];
        memset(hist, 0, sizeof(hist));
        double range = hi - lo;
        for (int64_t i = 0; i < nvox_t0; i++) {
            int b = (int)(((double)dst[i] - lo) / range * (NBINS - 1));
            if (b < 0) b = 0;
            if (b >= NBINS) b = NBINS - 1;
            hist[b]++;
        }
        int64_t p2 = nvox_t0 * 2 / 100, p98 = nvox_t0 * 98 / 100, sum = 0;
        int lo_bin = 0, hi_bin = NBINS - 1;
        for (int b = 0; b < NBINS; b++) {
            sum += hist[b];
            if (sum < p2)  lo_bin = b + 1;
            if (sum < p98) hi_bin = b;
        }
        if (lo_bin >= hi_bin) hi_bin = lo_bin + 1;
        slot->vmin = lo + range * lo_bin / NBINS;
        slot->vmax = lo + range * hi_bin / NBINS;
    } else if (lo < hi) {
        slot->vmin = lo;
        slot->vmax = hi;
    } else {
        slot->vmin = 0.0;
        slot->vmax = 1.0;
    }
    slot->auto_vmin = slot->vmin;
    slot->auto_vmax = slot->vmax;

    return dst;
}

int load_slots(App *app, CliArgs *args) {
    app->num_slots = 0;

    for (int i = 0; i < args->num_files; i++) {
        if (app->num_slots >= MAX_SLOTS) break;

        ImageSlot *slot = &app->slots[app->num_slots];
        memset(slot, 0, sizeof(*slot));

        nifti_image *nim = nifti_image_load(args->file_paths[i], 1);
        if (!nim) {
            fprintf(stderr, "ERROR: cannot load %s\n", args->file_paths[i]);
            continue;
        }

        slot->nim = nim;
        slot->nx  = (int)nim->nx;
        slot->ny  = (int)nim->ny;
        slot->nz  = (int)nim->nz;
        slot->nt  = (int)nim->nt;
        slot->dx  = nim->dx;
        slot->dy  = nim->dy;
        slot->dz  = nim->dz;
        slot->tr  = nim->dt;
        slot->cmap = 0;
        slot->ct   = 0;
        slot->zoom = 1.0;
        slot->zoom_sync = 0;
        slot->cx = slot->nx / 2;
        slot->cy = slot->ny / 2;
        slot->cz = slot->nz / 2;
        slot->cross_sync = 1;  /* default: follow global */

        /* basename for display */
        const char *fn = strrchr(args->file_paths[i], '/');
        fn = fn ? fn + 1 : args->file_paths[i];
        snprintf(slot->filename, sizeof(slot->filename), "%s", fn);

        slot->vol = convert_volume_float32(nim, slot);
        if (!slot->vol) {
            fprintf(stderr, "ERROR: conversion failed for %s\n", fn);
            nifti_image_free(nim);
            continue;
        }

        app->num_slots++;
#ifndef NDEBUG
        fprintf(stderr, "Loaded: %s  %dx%dx%dx%d\n", fn,
                slot->nx, slot->ny, slot->nz, slot->nt);
#endif
    }

    if (app->num_slots == 0) {
        fprintf(stderr, "ERROR: no images loaded\n");
        return 1;
    }

    /* load seg/overlay if provided */
    if (args->seg_path && app->num_slots > 0) {
        ImageSlot *slot = &app->slots[0];
        nifti_image *snim = nifti_image_load(args->seg_path, 1);
        if (snim) {
            slot->segs[0].nim = snim;
            slot->segs[0].nx  = (int)snim->nx;
            slot->segs[0].ny  = (int)snim->ny;
            slot->segs[0].nz  = (int)snim->nz;
            slot->segs[0].enabled = 1;
            slot->segs[0].opacity = 0.5f;
            slot->segs[0].label_mask = 0xFFFFFFFF;
            slot->num_segs = 1;

            float *svol = (float *)malloc((size_t)snim->nvox * sizeof(float));
            if (svol) {
                if (snim->datatype == NIFTI_TYPE_FLOAT32) {
                    memcpy(svol, snim->data, (size_t)snim->nvox * sizeof(float));
                } else if (snim->datatype == NIFTI_TYPE_INT16) {
                    const short *s = (const short *)snim->data;
                    for (int64_t j = 0; j < snim->nvox; j++) svol[j] = (float)s[j];
                }
                slot->segs[0].vol = svol;
            }
        }
    }
    if (args->ovl_path && app->num_slots > 0) {
        ImageSlot *slot = &app->slots[0];
        nifti_image *onim = nifti_image_load(args->ovl_path, 1);
        if (onim) {
            slot->ovls[0].nim = onim;
            slot->ovls[0].nx  = (int)onim->nx;
            slot->ovls[0].ny  = (int)onim->ny;
            slot->ovls[0].nz  = (int)onim->nz;
            slot->ovls[0].enabled = 1;
            slot->ovls[0].opacity = 0.5f;
            slot->num_ovls = 1;

            float *ovol = (float *)malloc((size_t)onim->nvox * sizeof(float));
            if (ovol) {
                if (onim->datatype == NIFTI_TYPE_FLOAT32) {
                    memcpy(ovol, onim->data, (size_t)onim->nvox * sizeof(float));
                } else if (onim->datatype == NIFTI_TYPE_INT16) {
                    const short *s = (const short *)onim->data;
                    for (int64_t j = 0; j < onim->nvox; j++) ovol[j] = (float)s[j];
                }
                slot->ovls[0].vol = ovol;
            }
        }
    }

    return 0;
}

int add_slot_from_file(App *app, const char *path) {
    if (app->num_slots >= MAX_SLOTS) return -1;

    ImageSlot *slot = &app->slots[app->num_slots];
    memset(slot, 0, sizeof(*slot));

    nifti_image *nim = nifti_image_load(path, 1);
    if (!nim) return -1;

    slot->nim = nim;
    slot->nx  = (int)nim->nx;
    slot->ny  = (int)nim->ny;
    slot->nz  = (int)nim->nz;
    slot->nt  = (int)nim->nt;
    slot->dx  = nim->dx;
    slot->dy  = nim->dy;
    slot->dz  = nim->dz;
    slot->tr  = nim->dt;
    slot->cmap = 0;
    slot->zoom = 1.0;
    slot->zoom_sync = 0;
    slot->cx = slot->nx / 2;
    slot->cy = slot->ny / 2;
    slot->cz = slot->nz / 2;
    slot->cross_sync = 1;  /* default: follow global */

    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    snprintf(slot->filename, sizeof(slot->filename), "%s", fn);

    int64_t nvox = nim->nvox;
    float *vol = (float *)malloc((size_t)nvox * sizeof(float));
    if (!vol) { nifti_image_free(nim); return -1; }

    if (nim->datatype == NIFTI_TYPE_FLOAT32 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        memcpy(vol, nim->data, (size_t)nvox * sizeof(float));
    } else if (nim->datatype == NIFTI_TYPE_FLOAT32) {
        const float *s = (const float *)nim->data;
        double slope = nim->scl_slope, inter = nim->scl_inter;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)((double)s[j] * slope + inter);
    } else if (nim->datatype == NIFTI_TYPE_INT16 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const short *s = (const short *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_INT16) {
        const short *s = (const short *)nim->data;
        double slope = nim->scl_slope, inter = nim->scl_inter;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)((double)s[j] * slope + inter);
    } else if (nim->datatype == NIFTI_TYPE_UINT16 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const unsigned short *s = (const unsigned short *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_UINT16) {
        const unsigned short *s = (const unsigned short *)nim->data;
        double slope = nim->scl_slope, inter = nim->scl_inter;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)((double)s[j] * slope + inter);
    } else if (nim->datatype == NIFTI_TYPE_UINT8 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const unsigned char *s = (const unsigned char *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_UINT8) {
        const unsigned char *s = (const unsigned char *)nim->data;
        double slope = nim->scl_slope, inter = nim->scl_inter;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)((double)s[j] * slope + inter);
    } else if (nim->datatype == NIFTI_TYPE_INT8 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const char *s = (const char *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_INT8) {
        const char *s = (const char *)nim->data;
        double slope = nim->scl_slope, inter = nim->scl_inter;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)((double)s[j] * slope + inter);
    } else if (nim->datatype == NIFTI_TYPE_FLOAT64) {
        const double *s = (const double *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else {
        for (int64_t j = 0; j < nvox; j++) {
            int64_t nvox_per_vol = (int64_t)slot->nx * slot->ny * slot->nz;
            int t = (int)(j / nvox_per_vol);
            if (t >= slot->nt) t = slot->nt - 1;
            int r = (int)(j % nvox_per_vol);
            vol[j] = (float)nifti_image_get_voxel(nim,
                r % slot->nx, (r / slot->nx) % slot->ny,
                (r / ((int64_t)slot->nx * slot->ny)), t);
        }
    }
    slot->vol = vol;

    float lo = vol[0], hi = vol[0];
    int64_t nvox_t0 = (int64_t)slot->nx * slot->ny * slot->nz;
    for (int64_t j = 0; j < nvox_t0 && j < nvox; j++) {
        if (vol[j] < lo) lo = vol[j];
        if (vol[j] > hi) hi = vol[j];
    }
    slot->vmin = slot->auto_vmin = lo;
    slot->vmax = slot->auto_vmax = hi;

    app->num_slots++;
#ifndef NDEBUG
    fprintf(stderr, "Dropped: %s  %dx%dx%dx%d\n", fn, slot->nx, slot->ny, slot->nz, slot->nt);
#endif
    return app->num_slots - 1;
}

int add_slot_from_pending(App *app) {
    if (app->num_slots >= MAX_SLOTS) return -1;
    if (!app->repl_pending_vol) return -1;

    ImageSlot *slot = &app->slots[app->num_slots];
    memset(slot, 0, sizeof(*slot));

    slot->nx  = app->repl_pending_nx;
    slot->ny  = app->repl_pending_ny;
    slot->nz  = app->repl_pending_nz;
    slot->nt  = app->repl_pending_nt;
    slot->dx  = app->repl_pending_dx;
    slot->dy  = app->repl_pending_dy;
    slot->dz  = app->repl_pending_dz;
    slot->tr  = app->repl_pending_tr;
    slot->vol = app->repl_pending_vol;
    slot->cmap = 0;
    slot->zoom = 1.0;
    slot->zoom_sync = 0;
    slot->cx = slot->nx / 2;
    slot->cy = slot->ny / 2;
    slot->cz = slot->nz / 2;
    slot->cross_sync = 1;
    snprintf(slot->filename, sizeof(slot->filename), "[VBL]");

    int64_t nvox = (int64_t)slot->nx * slot->ny * slot->nz * (slot->nt > 0 ? slot->nt : 1);
    int64_t nvox_t0 = (int64_t)slot->nx * slot->ny * slot->nz;
    float lo = slot->vol[0], hi = slot->vol[0];
    for (int64_t j = 0; j < nvox_t0 && j < nvox; j++) {
        if (slot->vol[j] < lo) lo = slot->vol[j];
        if (slot->vol[j] > hi) hi = slot->vol[j];
    }
    slot->vmin = slot->auto_vmin = lo;
    slot->vmax = slot->auto_vmax = hi;

    /* clear pending state */
    app->repl_pending_vol = NULL;
    app->repl_pending_has_data = 0;

    app->num_slots++;
    return app->num_slots - 1;
}

int add_attachment_to_slot(App *app, int slot_idx, const char *path, int is_seg) {
    if (slot_idx < 0 || slot_idx >= app->num_slots) return -1;
    ImageSlot *slot = &app->slots[slot_idx];
    Attachment *att;
    int *count;
    int max_count;

    if (is_seg) {
        if (slot->num_segs >= MAX_SEGS_PER_SLOT) return -1;
        att = &slot->segs[slot->num_segs];
        count = &slot->num_segs;
        max_count = MAX_SEGS_PER_SLOT;
    } else {
        if (slot->num_ovls >= MAX_OVLS_PER_SLOT) return -1;
        att = &slot->ovls[slot->num_ovls];
        count = &slot->num_ovls;
        max_count = MAX_OVLS_PER_SLOT;
    }

    memset(att, 0, sizeof(*att));
    nifti_image *nim = nifti_image_load(path, 1);
    if (!nim) return -1;

    att->nim = nim;
    att->nx  = (int)nim->nx;
    att->ny  = (int)nim->ny;
    att->nz  = (int)nim->nz;
    att->enabled = 1;
    att->opacity = 0.5f;
    att->threshold = 0.0f;
    att->label_mask = 0xFFFFFFFF;

    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    snprintf(att->filename, sizeof(att->filename), "%s", fn);

    int64_t nvox = nim->nvox;
    float *vol = (float *)malloc((size_t)nvox * sizeof(float));
    if (!vol) { nifti_image_free(nim); return -1; }

    if (nim->datatype == NIFTI_TYPE_FLOAT32 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        memcpy(vol, nim->data, (size_t)nvox * sizeof(float));
    } else if (nim->datatype == NIFTI_TYPE_INT16 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const short *s = (const short *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_UINT16 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const unsigned short *s = (const unsigned short *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else if (nim->datatype == NIFTI_TYPE_UINT8 && nim->scl_slope == 1.0 && nim->scl_inter == 0.0) {
        const unsigned char *s = (const unsigned char *)nim->data;
        for (int64_t j = 0; j < nvox; j++) vol[j] = (float)s[j];
    } else {
        for (int64_t j = 0; j < nvox; j++)
            vol[j] = (float)nifti_image_get_voxel(nim, j % att->nx,
                (j / att->nx) % att->ny, j / ((int64_t)att->nx * att->ny), 0);
    }
    att->vol = vol;
    (*count)++;

    /* update threshold max */
    for (int64_t j = 0; j < (int64_t)att->nx * att->ny * att->nz; j++) {
        float av = fabsf(vol[j]);
        if (av > app->ovl_abs_max) app->ovl_abs_max = av;
    }
    if (app->ovl_abs_max < 1.0f) app->ovl_abs_max = 1.0f;

    /* scan for unique labels (seg only) */
    if (is_seg) {
        att->label_count = 0;
        int64_t nv = (int64_t)att->nx * att->ny * att->nz;
        int64_t step = nv / 50000 + 1;  /* sample ~50k voxels */
        for (int64_t j = 0; j < nv; j += step) {
            int lbl = (int)(vol[j] + 0.5f);
            if (lbl <= 0 || lbl > 19) continue;
            int found = 0;
            for (int k = 0; k < att->label_count; k++)
                if (att->labels[k] == lbl) { found = 1; break; }
            if (!found && att->label_count < 20)
                att->labels[att->label_count++] = lbl;
        }
        /* sort labels */
        for (int a = 0; a < att->label_count - 1; a++)
            for (int b = a + 1; b < att->label_count; b++)
                if (att->labels[a] > att->labels[b]) {
                    int tmp = att->labels[a];
                    att->labels[a] = att->labels[b];
                    att->labels[b] = tmp;
                }
    }

#ifndef NDEBUG
    fprintf(stderr, "  + %s: %s  %dx%dx%d\n", is_seg ? "Seg" : "Ovl", fn, att->nx, att->ny, att->nz);
#endif
    return *count - 1;
}
