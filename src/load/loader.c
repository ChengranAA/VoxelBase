#include <float.h>
#include <stdio.h>
#include <strings.h>
#include "app.h"
#include "cli/cli.h"

#define NBINS 4096
#define PROGRESSIVE_THRESHOLD_BYTES (512ULL * 1024ULL * 1024ULL)

int voxelbase_is_gz_path(const char *path) {
    size_t len = path ? strlen(path) : 0;
    return len >= 3 && strcasecmp(path + len - 3, ".gz") == 0;
}

size_t voxelbase_raw_data_bytes(const nifti_image *nim) {
    if (!nim || nim->nvox <= 0 || nim->nbyper <= 0) return 0;
    return (size_t)nim->nvox * (size_t)nim->nbyper;
}

int voxelbase_should_progressive_load(const nifti_image *nim, const char *path) {
    return nim && nim->nt > 1 && !voxelbase_is_gz_path(path) &&
           voxelbase_raw_data_bytes(nim) >= PROGRESSIVE_THRESHOLD_BYTES;
}

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
            int64_t nvox_per_vol = (int64_t)slot->nx * slot->ny * slot->nz;
            int64_t r = nvox_per_vol > 0 ? i % nvox_per_vol : i;
            int64_t t = nvox_per_vol > 0 ? i / nvox_per_vol : 0;
            float v = (float)nifti_image_get_voxel(nim, r % slot->nx,
                (r / slot->nx) % slot->ny,
                (r / ((int64_t)slot->nx * slot->ny)) % slot->nz, t);
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

static void init_slot_metadata(ImageSlot *slot, nifti_image *nim, const char *path) {
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
    slot->cross_sync = 1;
    slot->loaded_t_count = slot->nt > 0 ? slot->nt : 1;
    slot->full_ready = 1;
    snprintf(slot->source_path, sizeof(slot->source_path), "%s", path ? path : "");

    const char *fn = path ? strrchr(path, '/') : NULL;
    fn = fn ? fn + 1 : (path ? path : "[image]");
    snprintf(slot->filename, sizeof(slot->filename), "%s", fn);
}

static int read_first_volume_raw(nifti_image *nim, void **raw_out) {
    if (!nim || !nim->iname || nim->nx <= 0 || nim->ny <= 0 || nim->nz <= 0 || nim->nbyper <= 0)
        return -1;
    size_t n3 = (size_t)nim->nx * (size_t)nim->ny * (size_t)nim->nz;
    size_t bytes = n3 * (size_t)nim->nbyper;
    void *raw = malloc(bytes);
    if (!raw) return -1;

    FILE *fp = fopen(nim->iname, "rb");
    if (!fp) { free(raw); return -1; }
    if (fseeko(fp, (off_t)nim->iname_offset, SEEK_SET) != 0) {
        fclose(fp); free(raw); return -1;
    }
    size_t got = fread(raw, 1, bytes, fp);
    fclose(fp);
    if (got != bytes) { free(raw); return -1; }

    if (nim->byteorder != nifti_short_order() && nim->swapsize > 0)
        nifti_swap_Nbytes(n3, nim->swapsize, raw);

    *raw_out = raw;
    return 0;
}

static float *convert_first_volume_float32(nifti_image *nim, ImageSlot *slot) {
    void *raw = NULL;
    if (read_first_volume_raw(nim, &raw) != 0) return NULL;

    nifti_image view = *nim;
    view.nt = 1;
    view.nvox = (int64_t)slot->nx * slot->ny * slot->nz;
    view.data = raw;

    float *vol = convert_volume_float32(&view, slot);
    free(raw);
    return vol;
}

static void *progressive_worker_main(void *arg) {
    ProgressiveJob *job = (ProgressiveJob *)arg;
    nifti_image *nim = nifti_image_load(job->path, 1);
    float *vol = NULL;
    int success = 0;
    char err[128] = "background load failed";

    if (!nim || !nim->data) {
        snprintf(err, sizeof(err), "background full load failed");
    } else {
        ImageSlot tmp;
        memset(&tmp, 0, sizeof(tmp));
        init_slot_metadata(&tmp, nim, job->path);
        vol = convert_volume_float32(nim, &tmp);
        if (vol) success = 1;
        else snprintf(err, sizeof(err), "background conversion failed");
    }

    pthread_mutex_lock(&job->mutex);
    job->success = success;
    if (success) {
        job->full_nim = nim;
        job->full_vol = vol;
    } else {
        if (nim) nifti_image_free(nim);
        free(vol);
        snprintf(job->error, sizeof(job->error), "%s", err);
    }
    job->completed = 1;
    pthread_mutex_unlock(&job->mutex);
    return NULL;
}

static int start_progressive_worker(ImageSlot *slot, const char *path) {
    ProgressiveJob *job = (ProgressiveJob *)calloc(1, sizeof(*job));
    if (!job) return -1;
    pthread_mutex_init(&job->mutex, NULL);
    snprintf(job->path, sizeof(job->path), "%s", path);
    if (pthread_create(&job->thread, NULL, progressive_worker_main, job) != 0) {
        pthread_mutex_destroy(&job->mutex);
        free(job);
        return -1;
    }
    slot->progressive_job = job;
    slot->progressive = 1;
    slot->loading = 1;
    slot->full_ready = 0;
    slot->load_failed = 0;
    slot->loaded_t_count = 1;
    snprintf(slot->load_status, sizeof(slot->load_status), "loading full 4D in background");
    return 0;
}

static int load_slot_progressive(ImageSlot *slot, const char *path) {
    nifti_image *nim = nifti_image_load(path, 0);
    if (!nim) return -1;
    init_slot_metadata(slot, nim, path);

    slot->vol = convert_first_volume_float32(nim, slot);
    if (!slot->vol) {
        nifti_image_free(nim);
        return -1;
    }

    if (start_progressive_worker(slot, path) != 0) {
        slot->progressive = 1;
        slot->loading = 0;
        slot->full_ready = 0;
        slot->load_failed = 1;
        slot->loaded_t_count = 1;
        snprintf(slot->load_status, sizeof(slot->load_status), "background load start failed");
    }
    return 0;
}

void progressive_poll(App *app) {
    if (!app) return;
    for (int i = 0; i < app->num_slots; i++) {
        ImageSlot *slot = &app->slots[i];
        ProgressiveJob *job = slot->progressive_job;
        if (!job) continue;

        pthread_mutex_lock(&job->mutex);
        int completed = job->completed;
        int success = job->success;
        pthread_mutex_unlock(&job->mutex);
        if (!completed) continue;

        pthread_join(job->thread, NULL);
        if (success) {
            nifti_image_free(slot->nim);
            free(slot->vol);
            free(slot->ts_data);
            slot->ts_data = NULL;
            slot->ts_valid = 0;
            slot->nim = job->full_nim;
            slot->vol = job->full_vol;
            slot->loaded_t_count = slot->nt > 0 ? slot->nt : 1;
            slot->loading = 0;
            slot->full_ready = 1;
            slot->load_failed = 0;
            snprintf(slot->load_status, sizeof(slot->load_status), "full 4D ready");
            if (slot->ct >= slot->nt) slot->ct = 0;
            app->dirty_slices = 1;
            app->dirty_contrast = 1;
            app->force_texture_recreate = 1;
        } else {
            slot->loading = 0;
            slot->full_ready = 0;
            slot->load_failed = 1;
            slot->loaded_t_count = 1;
            slot->ct = 0;
            snprintf(slot->load_status, sizeof(slot->load_status), "%s", job->error[0] ? job->error : "background load failed");
            app->dirty_slices = 1;
        }
        pthread_mutex_destroy(&job->mutex);
        free(job);
        slot->progressive_job = NULL;
    }
}

void progressive_cleanup_slot(ImageSlot *slot) {
    if (!slot || !slot->progressive_job) return;
    ProgressiveJob *job = slot->progressive_job;
    pthread_join(job->thread, NULL);
    if (job->full_nim) nifti_image_free(job->full_nim);
    free(job->full_vol);
    pthread_mutex_destroy(&job->mutex);
    free(job);
    slot->progressive_job = NULL;
}

int load_slots(App *app, CliArgs *args) {
    app->num_slots = 0;

    for (int i = 0; i < args->num_files; i++) {
        if (app->num_slots >= MAX_SLOTS) break;

        ImageSlot *slot = &app->slots[app->num_slots];
        memset(slot, 0, sizeof(*slot));

        nifti_image *probe = nifti_image_load(args->file_paths[i], 0);
        if (!probe) {
            fprintf(stderr, "ERROR: cannot load %s\n", args->file_paths[i]);
            continue;
        }

        const char *fn = strrchr(args->file_paths[i], '/');
        fn = fn ? fn + 1 : args->file_paths[i];

        if (voxelbase_should_progressive_load(probe, args->file_paths[i])) {
            nifti_image_free(probe);
            if (load_slot_progressive(slot, args->file_paths[i]) != 0) {
                fprintf(stderr, "ERROR: progressive load failed for %s\n", fn);
                continue;
            }
        } else {
            nifti_image_free(probe);
            nifti_image *nim = nifti_image_load(args->file_paths[i], 1);
            if (!nim) {
                fprintf(stderr, "ERROR: cannot load %s\n", args->file_paths[i]);
                continue;
            }
            init_slot_metadata(slot, nim, args->file_paths[i]);
            slot->vol = convert_volume_float32(nim, slot);
            if (!slot->vol) {
                fprintf(stderr, "ERROR: conversion failed for %s\n", fn);
                nifti_image_free(nim);
                continue;
            }
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

            /* convert to FLOAT32 in-place so scl_slope / scl_inter
               scaling is baked into the data (slope→1, inter→0).
               After conversion we can just memcpy. */
            if (snim->datatype != NIFTI_TYPE_FLOAT32 ||
                snim->scl_slope != 1.0 || snim->scl_inter != 0.0) {
                nifti_image_convert_inplace(snim, NIFTI_TYPE_FLOAT32);
            }

            float *svol = (float *)malloc((size_t)snim->nvox * sizeof(float));
            if (svol) {
                memcpy(svol, snim->data, (size_t)snim->nvox * sizeof(float));
                slot->segs[0].vol = svol;

                /* update threshold max */
                for (int64_t j = 0; j < (int64_t)slot->segs[0].nx * slot->segs[0].ny * slot->segs[0].nz; j++) {
                    float av = fabsf(svol[j]);
                    if (av > app->ovl_abs_max) app->ovl_abs_max = av;
                }
                if (app->ovl_abs_max < 1.0f) app->ovl_abs_max = 1.0f;

                /* scan for unique labels */
                slot->segs[0].label_count = 0;
                int64_t nv = (int64_t)slot->segs[0].nx * slot->segs[0].ny * slot->segs[0].nz;
                int64_t step = nv / 50000 + 1;
                for (int64_t j = 0; j < nv; j += step) {
                    int lbl = (int)(svol[j] + 0.5f);
                    if (lbl <= 0 || lbl > 19) continue;
                    int found = 0;
                    for (int k = 0; k < slot->segs[0].label_count; k++)
                        if (slot->segs[0].labels[k] == lbl) { found = 1; break; }
                    if (!found && slot->segs[0].label_count < 20)
                        slot->segs[0].labels[slot->segs[0].label_count++] = lbl;
                }
                /* sort labels */
                for (int a = 0; a < slot->segs[0].label_count - 1; a++)
                    for (int b = a + 1; b < slot->segs[0].label_count; b++)
                        if (slot->segs[0].labels[a] > slot->segs[0].labels[b]) {
                            int tmp = slot->segs[0].labels[a];
                            slot->segs[0].labels[a] = slot->segs[0].labels[b];
                            slot->segs[0].labels[b] = tmp;
                        }
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

            /* convert to FLOAT32 so scaling is baked in */
            if (onim->datatype != NIFTI_TYPE_FLOAT32 ||
                onim->scl_slope != 1.0 || onim->scl_inter != 0.0) {
                nifti_image_convert_inplace(onim, NIFTI_TYPE_FLOAT32);
            }

            float *ovol = (float *)malloc((size_t)onim->nvox * sizeof(float));
            if (ovol) {
                memcpy(ovol, onim->data, (size_t)onim->nvox * sizeof(float));
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

    nifti_image *probe = nifti_image_load(path, 0);
    if (!probe) return -1;
    if (voxelbase_should_progressive_load(probe, path)) {
        nifti_image_free(probe);
        if (load_slot_progressive(slot, path) != 0) return -1;
        app->num_slots++;
#ifndef NDEBUG
        fprintf(stderr, "Dropped progressive: %s  %dx%dx%dx%d\n", slot->filename, slot->nx, slot->ny, slot->nz, slot->nt);
#endif
        return app->num_slots - 1;
    }
    nifti_image_free(probe);

    nifti_image *nim = nifti_image_load(path, 1);
    if (!nim) return -1;

    init_slot_metadata(slot, nim, path);

    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;

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
    slot->loaded_t_count = slot->nt > 0 ? slot->nt : 1;
    slot->full_ready = 1;
    snprintf(slot->load_status, sizeof(slot->load_status), "full volume ready");
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

    /* convert to FLOAT32 so scl_slope / scl_inter scaling is
       baked into the data (slope->1, inter->0). */
    if (nim->datatype != NIFTI_TYPE_FLOAT32 ||
        nim->scl_slope != 1.0 || nim->scl_inter != 0.0) {
        nifti_image_convert_inplace(nim, NIFTI_TYPE_FLOAT32);
    }
    memcpy(vol, nim->data, (size_t)nvox * sizeof(float));
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
