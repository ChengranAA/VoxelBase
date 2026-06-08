/*
 *  InstaCorr Plugin — instantaneous correlation mapping
 *
 *  This plugin replaces the legacy src/instacorr/instacorr.c module.
 *  All per-slot state lives in InstaCorrSlot, allocated on init.
 */

#include "core/app.h"
#include "core/plugin.h"
#include "widgets/widgets.h"

/* ==================================================================
 *  Constants (same as legacy module)
 * ================================================================== */
#define CORR_CLIP_FRACTION 0.3f
#define CORR_SMOOTH_FWHM   0.2f
#define CORR_PEELS         1
#define CORR_NN            2

/* ==================================================================
 *  Per-slot state
 * ================================================================== */
typedef struct {
    int   corr_mode;
    int   corr_setup_done;
    double *corr_sum, *corr_sum2, *corr_std, *corr_std_dt;
    uint8_t *corr_mask;
    int64_t *corr_idx;
    int   corr_nbrain;
    double *corr_map;
    int   corr_seed_x, corr_seed_y, corr_seed_z;
    int   corr_seed_valid;
    float *corr_seed_ts;
    float corr_thresh;
    float *corr_vol_backup;
    float *corr_axial_buf, *corr_sagittal_buf, *corr_coronal_buf;
    float *corr_zoom_axial_buf, *corr_zoom_sagittal_buf, *corr_zoom_coronal_buf;
} InstaCorrSlot;

typedef struct {
    InstaCorrSlot *slots;
    int max_slots;
} InstaCorrState;

/* ==================================================================
 *  Forward declarations
 * ================================================================== */
static int  instacorr_setup(App *a, InstaCorrState *st);
static void instacorr_compute(App *a, InstaCorrState *st, int sx, int sy, int sz);
static void extract_corr_axial_slice(ImageSlot *cs, InstaCorrSlot *c, int z);
static void extract_corr_sagittal_slice(ImageSlot *cs, InstaCorrSlot *c, int x);
static void extract_corr_coronal_slice(ImageSlot *cs, InstaCorrSlot *c, int y);

extern void draw_corr_overlay(gf_fb *fb, gf_irect r, const float *corr,
                              int corr_w, int corr_h, double thresh);

/* ═══════════════════════════════════════════════════════════════════
 *  instacorr_setup  —  build brain mask & optionally smooth
 * ═══════════════════════════════════════════════════════════════════ */
#define MHIST_NBINS 4096

static int instacorr_setup(App *a, InstaCorrState *st) {
    ImageSlot *cs = &CUR_SLOT(a);
    InstaCorrSlot *c = &st->slots[a->active_slot];
    if (cs->nt < 2) return 0;

    int64_t nvox = (int64_t)cs->nx * cs->ny * cs->nz;
    int     nt   = cs->nt;

    /* allocate */
    c->corr_sum  = (double *)malloc((size_t)nvox * sizeof(double));
    c->corr_sum2 = (double *)malloc((size_t)nvox * sizeof(double));
    c->corr_std  = (double *)malloc((size_t)nvox * sizeof(double));
    c->corr_mask = (uint8_t *)malloc((size_t)nvox);
    c->corr_map  = (double *)malloc((size_t)nvox * sizeof(double));
    if (!c->corr_sum || !c->corr_sum2 || !c->corr_std || !c->corr_mask || !c->corr_map) {
        fprintf(stderr, "InstaCorr: out of memory during setup\n");
        free(c->corr_sum); free(c->corr_sum2); free(c->corr_std);
        free(c->corr_mask); free(c->corr_map);
        c->corr_sum = c->corr_sum2 = c->corr_std = NULL;
        c->corr_mask = NULL; c->corr_map = NULL;
        return 0;
    }

    fprintf(stderr, "InstaCorr: setting up...");

    /* ---- pass 1: compute ΣY and ΣY² for every voxel ---- */
    memset(c->corr_sum,  0, (size_t)nvox * sizeof(double));
    memset(c->corr_sum2, 0, (size_t)nvox * sizeof(double));

    for (int t = 0; t < nt; t++) {
        const float *vol_t = cs->vol + (size_t)t * nvox;
        for (int64_t v = 0; v < nvox; v++) {
            double y = (double)vol_t[v];
            c->corr_sum[v]  += y;
            c->corr_sum2[v] += y * y;
        }
    }

    /* ---- pass 2: compute std dev and brain mask ---- */
    float inv_n = 1.0f / (float)nt;
    double *mean_buf = (double *)malloc((size_t)nvox * sizeof(double));
    if (!mean_buf) {
        fprintf(stderr, "InstaCorr: out of memory (mean_buf)\n");
        free(c->corr_sum); free(c->corr_sum2); free(c->corr_std);
        free(c->corr_mask); free(c->corr_map);
        c->corr_sum = c->corr_sum2 = c->corr_std = NULL;
        c->corr_mask = NULL; c->corr_map = NULL;
        return 0;
    }

    double mean_lo = DBL_MAX, mean_hi = -DBL_MAX;
    for (int64_t v = 0; v < nvox; v++) {
        double s  = c->corr_sum[v];
        double s2 = c->corr_sum2[v];
        double mn = s * inv_n;
        double var = s2 * inv_n - mn * mn;
        if (var < 0.0) var = 0.0;
        c->corr_std[v] = sqrt(var);
        mean_buf[v] = mn;
        if (mean_buf[v] < mean_lo) mean_lo = mean_buf[v];
        if (mean_buf[v] > mean_hi) mean_hi = mean_buf[v];
    }

    /* ---- build histogram of mean values for thresholding ---- */
    int mhist[MHIST_NBINS];
    memset(mhist, 0, sizeof(mhist));
    double mrange = mean_hi - mean_lo;
    if (mrange < 1e-12) mrange = 1.0;
    for (int64_t v = 0; v < nvox; v++) {
        int b = (int)((mean_buf[v] - mean_lo) / mrange * (MHIST_NBINS - 1));
        if (b < 0) b = 0;
        if (b >= MHIST_NBINS) b = MHIST_NBINS - 1;
        mhist[b]++;
    }

    /* find first major peak from low end (background); skip noise bins */
    int bg_peak_bin = 0, min_cnt = (int)((double)nvox * 0.0005);
    if (min_cnt < 1) min_cnt = 1;
    for (int b = 0; b < MHIST_NBINS - 1; b++) {
        if (mhist[b] >= min_cnt && mhist[b] >= mhist[b + 1]) {
            bg_peak_bin = b; break;
        }
    }
    double peak_val = mean_lo + mrange * (double)bg_peak_bin / (double)(MHIST_NBINS - 1);

    /* robust max: 99th percentile */
    int64_t csum = 0, p99_target = nvox * 99 / 100;
    int p99_bin = MHIST_NBINS - 1;
    for (int b = 0; b < MHIST_NBINS; b++) {
        csum += mhist[b];
        if (csum >= p99_target) { p99_bin = b; break; }
    }
    double robust_max = mean_lo + mrange * (double)p99_bin / (double)(MHIST_NBINS - 1);

    double mean_thresh = peak_val + (double)CORR_CLIP_FRACTION * (robust_max - peak_val);
    fprintf(stderr, "  bg-peak=%.2f  robust-max=%.2f  thresh=%.2f  (clip=%.2f)\n",
            peak_val, robust_max, mean_thresh, (double)CORR_CLIP_FRACTION);

    c->corr_nbrain = 0;
    for (int64_t v = 0; v < nvox; v++) {
        int in_brain = (mean_buf[v] > mean_thresh && c->corr_std[v] > 1e-8) ? 1 : 0;
        c->corr_mask[v] = (uint8_t)in_brain;
        c->corr_nbrain += in_brain;
    }
    fprintf(stderr, "  raw mask: %d vox\n", c->corr_nbrain);

    /* ---- erode -> largest component -> dilate -> hole fill -> closing ---- */
    {
        int nx = cs->nx, ny = cs->ny, nz = cs->nz;

        /* build NN-neighbour offset list */
        int nn_cnt = 0, nn_off[26];
        for (int dz = -1; dz <= 1; dz++) {
          for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                int mh = abs(dx) + abs(dy) + abs(dz);
                int ch = abs(dx);
                if (abs(dy) > ch) ch = abs(dy);
                if (abs(dz) > ch) ch = abs(dz);
                int ok = 0;
                if (CORR_NN == 1) ok = (mh == 1);
                if (CORR_NN == 2) ok = (ch == 1 && mh <= 2);
                if (CORR_NN == 3) ok = 1;
                if (ok) nn_off[nn_cnt++] = (int)(dx + (int64_t)dy * nx + (int64_t)dz * nx * ny);
            }
          }
        }

        uint8_t *tmp = (uint8_t *)malloc((size_t)nvox);
        if (!tmp) {
            fprintf(stderr, "  morph: out of memory, skipping\n");
            goto morph_skip;
        }

        /* ---- step 1: erode ---- */
        for (int peel = 0; peel < CORR_PEELS; peel++) {
            memcpy(tmp, c->corr_mask, (size_t)nvox);
            for (int z = 0; z < nz; z++) {
              for (int y = 0; y < ny; y++) {
                for (int x = 0; x < nx; x++) {
                    int64_t v = (int64_t)z * nx * ny + (int64_t)y * nx + x;
                    if (!tmp[v]) continue;
                    for (int n = 0; n < nn_cnt; n++) {
                        int64_t vn = v + nn_off[n];
                        if (vn < 0 || vn >= nvox) { c->corr_mask[v] = 0; break; }
                        int nz_ = (int)(vn / (nx * ny));
                        int ny_ = (int)((vn % (nx * ny)) / nx);
                        int nx_ = (int)(vn % nx);
                        if (abs(nz_ - z) > 1 || abs(ny_ - y) > 1 || abs(nx_ - x) > 1)
                            { c->corr_mask[v] = 0; break; }
                        if (!tmp[vn]) { c->corr_mask[v] = 0; break; }
                    }
                }
              }
            }
        }
        c->corr_nbrain = 0;
        for (int64_t v = 0; v < nvox; v++) c->corr_nbrain += c->corr_mask[v];
        fprintf(stderr, "  after erode: %d vox\n", c->corr_nbrain);

        /* ---- step 2: keep largest connected component ---- */
        {
            uint8_t *visited = (uint8_t *)calloc((size_t)nvox, 1);
            if (visited) {
                int64_t q_cap = (int64_t)c->corr_nbrain + 64;
                if (q_cap < 4096) q_cap = 4096;
                int64_t *queue = (int64_t *)malloc((size_t)q_cap * sizeof(int64_t));
                if (queue) {
                    int64_t best_size = 0;
                    uint8_t  best_label = 0;
                    uint8_t  comp_id = 1;
                    fprintf(stderr, "  searching components...\n");

                    for (int64_t v = 0; v < nvox; v++) {
                        if (!c->corr_mask[v] || visited[v]) continue;
                        comp_id++;
                        uint8_t label = comp_id; if (label == 0) label = 254;
                        int64_t head = 0, tail = 0;
                        queue[tail++] = v;
                        visited[v] = label;
                        int64_t comp_size = 0;
                        while (head < tail) {
                            int64_t cur = queue[head++];
                            comp_size++;
                            if (comp_size % 100000 == 0)
                                fprintf(stderr, "  component %d: %lld vox...\n",
                                        comp_id, (long long)comp_size);
                            int cz = (int)(cur / (nx * ny));
                            int cy = (int)((cur % (nx * ny)) / nx);
                            int cx = (int)(cur % nx);
                            for (int n = 0; n < nn_cnt; n++) {
                                int64_t vn = cur + nn_off[n];
                                if (vn < 0 || vn >= nvox) continue;
                                int nz_ = (int)(vn / (nx * ny));
                                int ny2 = (int)((vn % (nx * ny)) / nx);
                                int nx_ = (int)(vn % nx);
                                if (abs(nz_ - cz) > 1 || abs(ny2 - cy) > 1 || abs(nx_ - cx) > 1) continue;
                                if (c->corr_mask[vn] && !visited[vn]) {
                                    visited[vn] = label;
                                    queue[tail++] = vn;
                                }
                            }
                        }
                        if (comp_size > best_size) {
                            best_size = comp_size;
                            best_label = label;
                        }
                    }

                    if (best_label > 0) {
                        for (int64_t v = 0; v < nvox; v++)
                            c->corr_mask[v] = (visited[v] == best_label) ? 1 : 0;
                        fprintf(stderr, "  largest component: %lld vox\n", (long long)best_size);
                    }
                    free(queue);
                }
                free(visited);
            }
        }

        /* ---- step 3: dilate ---- */
        fprintf(stderr, "  dilating...\n");
        for (int peel = 0; peel < CORR_PEELS; peel++) {
            memcpy(tmp, c->corr_mask, (size_t)nvox);
            for (int z = 0; z < nz; z++) {
              for (int y = 0; y < ny; y++) {
                for (int x = 0; x < nx; x++) {
                    int64_t v = (int64_t)z * nx * ny + (int64_t)y * nx + x;
                    if (tmp[v]) continue;
                    for (int n = 0; n < nn_cnt; n++) {
                        int64_t vn = v + nn_off[n];
                        if (vn < 0 || vn >= nvox) continue;
                        int nz_ = (int)(vn / (nx * ny));
                        int ny_ = (int)((vn % (nx * ny)) / nx);
                        int nx_ = (int)(vn % nx);
                        if (abs(nz_ - z) > 1 || abs(ny_ - y) > 1 || abs(nx_ - x) > 1) continue;
                        if (tmp[vn]) { c->corr_mask[v] = 1; break; }
                    }
                }
              }
            }
        }
        c->corr_nbrain = 0;
        for (int64_t v = 0; v < nvox; v++) c->corr_nbrain += c->corr_mask[v];
        fprintf(stderr, "  after dilate: %d vox\n", c->corr_nbrain);

        /* ---- step 4: hole filling ---- */
        {
            uint8_t *visited = (uint8_t *)calloc((size_t)nvox, 1);
            if (visited) {
                int64_t q_cap = nvox;
                int64_t *queue = (int64_t *)malloc((size_t)q_cap * sizeof(int64_t));
                if (queue) {
                    int64_t head = 0, tail = 0;

                    /* seed all six faces with background voxels */
                    for (int x = 0; x < nx; x++) {
                        for (int y = 0; y < ny; y++) {
                            int64_t v0 = (int64_t)0 * nx * ny + (int64_t)y * nx + x;
                            int64_t v1 = (int64_t)(nz-1) * nx * ny + (int64_t)y * nx + x;
                            if (!c->corr_mask[v0] && !visited[v0]) { visited[v0]=1; queue[tail++]=v0; }
                            if (!c->corr_mask[v1] && !visited[v1]) { visited[v1]=1; queue[tail++]=v1; }
                        }
                    }
                    for (int x = 0; x < nx; x++) {
                        for (int z = 0; z < nz; z++) {
                            int64_t v0 = (int64_t)z * nx * ny + (int64_t)0 * nx + x;
                            int64_t v1 = (int64_t)z * nx * ny + (int64_t)(ny-1) * nx + x;
                            if (!c->corr_mask[v0] && !visited[v0]) { visited[v0]=1; queue[tail++]=v0; }
                            if (!c->corr_mask[v1] && !visited[v1]) { visited[v1]=1; queue[tail++]=v1; }
                        }
                    }
                    for (int y = 0; y < ny; y++) {
                        for (int z = 0; z < nz; z++) {
                            int64_t v0 = (int64_t)z * nx * ny + (int64_t)y * nx + 0;
                            int64_t v1 = (int64_t)z * nx * ny + (int64_t)y * nx + (nx-1);
                            if (!c->corr_mask[v0] && !visited[v0]) { visited[v0]=1; queue[tail++]=v0; }
                            if (!c->corr_mask[v1] && !visited[v1]) { visited[v1]=1; queue[tail++]=v1; }
                        }
                    }

                    /* BFS from border */
                    while (head < tail) {
                        int64_t cur = queue[head++];
                        int cz = (int)(cur / (nx * ny));
                        int cy = (int)((cur % (nx * ny)) / nx);
                        int cx = (int)(cur % nx);
                        for (int n = 0; n < nn_cnt; n++) {
                            int64_t vn = cur + nn_off[n];
                            if (vn < 0 || vn >= nvox) continue;
                            int nz_ = (int)(vn / (nx * ny));
                            int ny2 = (int)((vn % (nx * ny)) / nx);
                            int nx_ = (int)(vn % nx);
                            if (abs(nz_ - cz) > 1 || abs(ny2 - cy) > 1 || abs(nx_ - cx) > 1) continue;
                            if (!c->corr_mask[vn] && !visited[vn]) {
                                visited[vn] = 1;
                                queue[tail++] = vn;
                            }
                        }
                    }

                    /* unreached background = holes */
                    {
                        int64_t filled = 0;
                        for (int64_t v = 0; v < nvox; v++) {
                            if (!c->corr_mask[v] && !visited[v]) {
                                c->corr_mask[v] = 1;
                                filled++;
                            }
                        }
                        fprintf(stderr, "  holes filled: %lld\n", (long long)filled);
                    }
                    free(queue);
                }
                free(visited);
            }
        }

        /* ---- step 5: closing (dilate then erode) ---- */
        for (int peel = 0; peel < CORR_PEELS; peel++) {
            /* dilate */
            memcpy(tmp, c->corr_mask, (size_t)nvox);
            for (int z = 0; z < nz; z++) {
              for (int y = 0; y < ny; y++) {
                for (int x = 0; x < nx; x++) {
                    int64_t v = (int64_t)z * nx * ny + (int64_t)y * nx + x;
                    if (tmp[v]) continue;
                    for (int n = 0; n < nn_cnt; n++) {
                        int64_t vn = v + nn_off[n];
                        if (vn < 0 || vn >= nvox) continue;
                        int nz_ = (int)(vn / (nx * ny));
                        int ny_ = (int)((vn % (nx * ny)) / nx);
                        int nx_ = (int)(vn % nx);
                        if (abs(nz_ - z) > 1 || abs(ny_ - y) > 1 || abs(nx_ - x) > 1) continue;
                        if (tmp[vn]) { c->corr_mask[v] = 1; break; }
                    }
                }
              }
            }
            /* erode */
            memcpy(tmp, c->corr_mask, (size_t)nvox);
            for (int z = 0; z < nz; z++) {
              for (int y = 0; y < ny; y++) {
                for (int x = 0; x < nx; x++) {
                    int64_t v = (int64_t)z * nx * ny + (int64_t)y * nx + x;
                    if (!tmp[v]) continue;
                    for (int n = 0; n < nn_cnt; n++) {
                        int64_t vn = v + nn_off[n];
                        if (vn < 0 || vn >= nvox) { c->corr_mask[v] = 0; break; }
                        int nz_ = (int)(vn / (nx * ny));
                        int ny_ = (int)((vn % (nx * ny)) / nx);
                        int nx_ = (int)(vn % nx);
                        if (abs(nz_ - z) > 1 || abs(ny_ - y) > 1 || abs(nx_ - x) > 1)
                            { c->corr_mask[v] = 0; break; }
                        if (!tmp[vn]) { c->corr_mask[v] = 0; break; }
                    }
                }
              }
            }
        }

        free(tmp);

        /* final recount */
        c->corr_nbrain = 0;
        for (int64_t v = 0; v < nvox; v++) c->corr_nbrain += c->corr_mask[v];
        fprintf(stderr, "  after closing: %d vox\n", c->corr_nbrain);

        morph_skip:;
    }

    /* build flat index of masked voxels for fast dot-product */
    c->corr_idx = (int64_t *)malloc((size_t)c->corr_nbrain * sizeof(int64_t));
    if (c->corr_idx) {
        int64_t k = 0;
        for (int64_t v = 0; v < nvox; v++)
            if (c->corr_mask[v]) c->corr_idx[k++] = v;
    }

    /* ---- spatial smoothing: separable 3-pass 1D Gaussian, masked (in-place) ---- */
    if (CORR_SMOOTH_FWHM > 0.0f && c->corr_nbrain > 0) {
        int nx = cs->nx, ny = cs->ny, nz = cs->nz;
        int nt = cs->nt;
        /* backup raw data for restore on instacorr exit */
        int64_t nvox_total = (int64_t)nvox * nt;
        c->corr_vol_backup = (float *)malloc((size_t)nvox_total * sizeof(float));
        if (c->corr_vol_backup)
            memcpy(c->corr_vol_backup, cs->vol, (size_t)nvox_total * sizeof(float));
        double sigma_mm = (double)CORR_SMOOTH_FWHM / 2.354820045;
        double sx = sigma_mm / cs->dx, sy = sigma_mm / cs->dy, sz = sigma_mm / cs->dz;
        int krx = (int)ceil(3.0 * sx); if (krx < 1) krx = 1;
        int kry = (int)ceil(3.0 * sy); if (kry < 1) kry = 1;
        int krz = (int)ceil(3.0 * sz); if (krz < 1) krz = 1;
        int kw_max = 2*krx+1; if (2*kry+1>kw_max) kw_max=2*kry+1; if (2*krz+1>kw_max) kw_max=2*krz+1;
        double *kx = (double *)malloc((size_t)kw_max * sizeof(double));
        double *ky = (double *)malloc((size_t)kw_max * sizeof(double));
        double *kz = (double *)malloc((size_t)kw_max * sizeof(double));
        float  *buf = (float  *)malloc((size_t)nvox * sizeof(float));
        if (kx && ky && kz && buf) {
            double ksx=0,ksy=0,ksz=0;
            for (int i=-krx;i<=krx;i++){kx[i+krx]=exp(-0.5*i*i/(sx*sx));ksx+=kx[i+krx];}
            for (int i=-kry;i<=kry;i++){ky[i+kry]=exp(-0.5*i*i/(sy*sy));ksy+=ky[i+kry];}
            for (int i=-krz;i<=krz;i++){kz[i+krz]=exp(-0.5*i*i/(sz*sz));ksz+=kz[i+krz];}
            for (int i=0;i<2*krx+1;i++)kx[i]/=ksx;
            for (int i=0;i<2*kry+1;i++)ky[i]/=ksy;
            for (int i=0;i<2*krz+1;i++)kz[i]/=ksz;
            fprintf(stderr,"  smoothing FWHM=%.1f mm  sigma=(%.2f,%.2f,%.2f) vox\n",
                    (double)CORR_SMOOTH_FWHM,sx,sy,sz);
            int64_t nxy=(int64_t)nx*ny;
            for (int t=0;t<nt;t++){
                float *vol_t=cs->vol+(size_t)t*nvox;
                memcpy(buf,vol_t,(size_t)nvox*sizeof(float));
                for(int z=0;z<nz;z++)for(int y=0;y<ny;y++){int64_t rb=(int64_t)z*nxy+(int64_t)y*nx;
                  for(int x=0;x<nx;x++){int64_t v=rb+x;if(!c->corr_mask[v])continue;double s=0,w=0;
                    for(int i=-krx;i<=krx;i++){int xi=x+i;if(xi<0)xi=0;if(xi>=nx)xi=nx-1;
                      int64_t vn=rb+xi;if(c->corr_mask[vn]){s+=(double)buf[vn]*kx[i+krx];w+=kx[i+krx];}}
                    vol_t[v]=w>0?(float)(s/w):(float)s;}}
                memcpy(buf,vol_t,(size_t)nvox*sizeof(float));
                for(int z=0;z<nz;z++)for(int y=0;y<ny;y++){int64_t rb=(int64_t)z*nxy+(int64_t)y*nx;
                  for(int x=0;x<nx;x++){int64_t v=rb+x;if(!c->corr_mask[v])continue;double s=0,w=0;
                    for(int i=-kry;i<=kry;i++){int yi=y+i;if(yi<0)yi=0;if(yi>=ny)yi=ny-1;
                      int64_t vn=(int64_t)z*nxy+(int64_t)yi*nx+x;if(c->corr_mask[vn]){s+=(double)buf[vn]*ky[i+kry];w+=ky[i+kry];}}
                    vol_t[v]=w>0?(float)(s/w):(float)s;}}
                memcpy(buf,vol_t,(size_t)nvox*sizeof(float));
                for(int z=0;z<nz;z++)for(int y=0;y<ny;y++){int64_t rb=(int64_t)z*nxy+(int64_t)y*nx;
                  for(int x=0;x<nx;x++){int64_t v=rb+x;if(!c->corr_mask[v])continue;double s=0,w=0;
                    for(int i=-krz;i<=krz;i++){int zi=z+i;if(zi<0)zi=0;if(zi>=nz)zi=nz-1;
                      int64_t vn=(int64_t)zi*nxy+(int64_t)y*nx+x;if(c->corr_mask[vn]){s+=(double)buf[vn]*kz[i+krz];w+=kz[i+krz];}}
                    vol_t[v]=w>0?(float)(s/w):(float)s;}}
            }
        }
        free(kx);free(ky);free(kz);free(buf);

        /* recompute stats from smoothed data for Pearson denominator */
        {
            float inv_n_r = 1.0f / (float)nt;
            for (int64_t v = 0; v < nvox; v++) {
                if (!c->corr_mask[v]) continue;
                double s=0.0, s2=0.0;
                for (int t=0; t<nt; t++) {
                    double y = (double)cs->vol[(size_t)t * nvox + v];
                    s  += y;
                    s2 += y * y;
                }
                c->corr_sum[v]  = s;
                c->corr_sum2[v] = s2;
                c->corr_std[v]  = sqrt(s2 * inv_n_r - (s * inv_n_r) * (s * inv_n_r));
                mean_buf[v]     = s * (double)inv_n_r;
            }
            fprintf(stderr, "  stats recomputed from smoothed data\n");
        }
    }
    free(mean_buf);
    #undef MHIST_NBINS

    c->corr_thresh      = 0.0f;
    c->corr_seed_valid  = 0;
    c->corr_setup_done  = 1;

    fprintf(stderr, " done.\n");
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  instacorr_compute  —  Pearson r from seed
 * ═══════════════════════════════════════════════════════════════════ */
static void instacorr_compute(App *a, InstaCorrState *st,
                              int sx, int sy, int sz) {
    ImageSlot *cs = &CUR_SLOT(a);
    InstaCorrSlot *c = &st->slots[a->active_slot];
    if (!c->corr_setup_done || cs->nt < 2) return;

    int64_t nvox = (int64_t)cs->nx * cs->ny * cs->nz;
    int     nt   = cs->nt;

    /* clamp seed coords */
    if (sx < 0) sx = 0; if (sx >= cs->nx) sx = cs->nx - 1;
    if (sy < 0) sy = 0; if (sy >= cs->ny) sy = cs->ny - 1;
    if (sz < 0) sz = 0; if (sz >= cs->nz) sz = cs->nz - 1;

    /* ---- extract seed time series ---- */
    size_t vox_off = (size_t)sx + (size_t)sy * cs->nx + (size_t)sz * cs->nx * cs->ny;
    float *X = c->corr_axial_buf;  /* reuse as scratch */
    if ((int64_t)cs->nx * cs->ny < nt) X = cs->ts_data;

    double sum_X = 0.0, sum_X2 = 0.0;
    for (int t = 0; t < nt; t++) {
        float v = cs->vol[(size_t)t * nvox + vox_off];
        X[t] = v;
        sum_X  += (double)v;
        sum_X2 += (double)v * (double)v;
    }

    /* copy seed time series for chart display */
    if (c->corr_seed_ts)
        for (int t = 0; t < nt; t++) c->corr_seed_ts[t] = X[t];

    double mean_X = sum_X / (double)nt;
    double var_X  = sum_X2 / (double)nt - mean_X * mean_X;
    if (var_X < 1e-12) {
        memset(c->corr_map, 0, (size_t)nvox * sizeof(double));
        c->corr_seed_x = sx; c->corr_seed_y = sy; c->corr_seed_z = sz;
        c->corr_seed_valid = 1;
        return;
    }
    double std_X = sqrt(var_X);

    /* ---- accumulate dot products (masked voxels only) ---- */
    memset(c->corr_map, 0, (size_t)nvox * sizeof(double));
    if (c->corr_idx) {
        for (int t = 0; t < nt; t++) {
            double alpha = (double)X[t];
            const float *vol_t = cs->vol + (size_t)t * nvox;
            for (int64_t k = 0; k < c->corr_nbrain; k++) {
                int64_t v = c->corr_idx[k];
                c->corr_map[v] += alpha * (double)vol_t[v];
            }
        }
    }

    /* ---- finalize Pearson r ---- */
    float inv_n   = 1.0f / (float)nt;
    double sum_X_d = sum_X;
    double denom_X = (double)nt * std_X;

    for (int64_t v = 0; v < nvox; v++) {
        if (!c->corr_mask[v]) { c->corr_map[v] = -1.0; continue; }
        double num   = c->corr_map[v] - sum_X_d * c->corr_sum[v] * (double)inv_n;
        double denom = denom_X * c->corr_std[v];
        if (denom > 1e-10) {
            double r = num / denom;
            if (r >  1.0) r =  1.0;
            if (r < -1.0) r = -1.0;
            c->corr_map[v] = r;
        } else {
            c->corr_map[v] = 0.0;
        }
    }

    c->corr_seed_x = sx; c->corr_seed_y = sy; c->corr_seed_z = sz;
    c->corr_seed_valid = 1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Correlation map slice extraction
 * ═══════════════════════════════════════════════════════════════════ */

static void extract_corr_axial_slice(ImageSlot *cs, InstaCorrSlot *c, int z) {
    if (!c->corr_map) return;
    int nx = cs->nx, ny = cs->ny;
    if (z < 0) z = 0; if (z >= cs->nz) z = cs->nz - 1;
    size_t base = (size_t)z * nx * ny;
    for (int row = 0; row < ny; row++) {
        int src_y = ny - 1 - row;
        for (int col = 0; col < nx; col++)
            c->corr_axial_buf[(size_t)row * nx + col] =
                (float)c->corr_map[base + (size_t)src_y * nx + col];
    }
}

static void extract_corr_sagittal_slice(ImageSlot *cs, InstaCorrSlot *c, int x) {
    if (!c->corr_map) return;
    int nx = cs->nx, ny = cs->ny, nz = cs->nz;
    if (x < 0) x = 0; if (x >= nx) x = nx - 1;
    for (int row = 0; row < nz; row++) {
        int src_z = nz - 1 - row;
        float *dst = c->corr_sagittal_buf + (size_t)row * ny;
        for (int col = 0; col < ny; col++) {
            int src_y = ny - 1 - col;
            dst[col] = (float)c->corr_map[(size_t)x + (size_t)src_y * nx
                                   + (size_t)src_z * nx * ny];
        }
    }
}

static void extract_corr_coronal_slice(ImageSlot *cs, InstaCorrSlot *c, int y) {
    if (!c->corr_map) return;
    int nx = cs->nx, nz = cs->nz;
    if (y < 0) y = 0; if (y >= cs->ny) y = cs->ny - 1;
    size_t base = (size_t)y * nx;
    for (int row = 0; row < nz; row++) {
        int src_z = nz - 1 - row;
        for (int col = 0; col < nx; col++)
            c->corr_coronal_buf[(size_t)row * nx + col] =
                (float)c->corr_map[base + (size_t)src_z * nx * cs->ny + col];
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Plugin hooks
 * ═══════════════════════════════════════════════════════════════════ */

Plugin instacorr_plugin;

static int ica_init(App *a) {
    InstaCorrState *st = (InstaCorrState *)calloc(1, sizeof(InstaCorrState));
    if (!st) return 1;
    st->max_slots = a->num_slots > 0 ? a->num_slots : MAX_PLUGINS;
    st->slots = (InstaCorrSlot *)calloc((size_t)st->max_slots, sizeof(InstaCorrSlot));
    if (!st->slots) { free(st); return 1; }
    instacorr_plugin.ctx = st;
    return 0;
}

static int ica_keyboard(App *a, int key, int mod, int pressed) {
    (void)mod;
    if (!pressed) return 0;
    InstaCorrState *st = (InstaCorrState *)instacorr_plugin.ctx;
    if (!st) return 0;
    if (a->active_slot < 0 || a->active_slot >= st->max_slots) return 0;
    InstaCorrSlot *c = &st->slots[a->active_slot];

    if (key == BK_KEY_I) {
        ImageSlot *cs = &CUR_SLOT(a);
        if (!c->corr_setup_done && cs->nt > 1) {
            /* lazy setup on first press */
            if (!instacorr_setup(a, st)) {
                fprintf(stderr, "InstaCorr: setup failed\n");
                return 1; /* consumed even on failure */
            }
        }
        if (c->corr_setup_done) {
            c->corr_mode = !c->corr_mode;
            /* swap raw/smoothed data on toggle */
            if (c->corr_vol_backup) {
                float *tmp = cs->vol;
                cs->vol = c->corr_vol_backup;
                c->corr_vol_backup = tmp;
            }
        } else {
            fprintf(stderr, "InstaCorr: not available (need 4D dataset)\n");
        }
        return 1; /* consumed */
    }

    if (key == BK_KEY_U) {
        if (c->corr_mode) { c->corr_thresh += 0.05f; if (c->corr_thresh > 0.95f) c->corr_thresh = 0.95f; }
        return 1;
    }

    if (key == BK_KEY_J) {
        if (c->corr_mode) { c->corr_thresh -= 0.05f; if (c->corr_thresh < 0.0f) c->corr_thresh = 0.0f; }
        return 1;
    }

    return 0;
}

static int ica_mouse_click(App *a, int btn, int mod, int pressed) {
    (void)mod;
    if (btn != BK_MOUSE_LEFT || !pressed) return 0;
    InstaCorrState *st = (InstaCorrState *)instacorr_plugin.ctx;
    if (!st) return 0;
    if (a->active_slot < 0 || a->active_slot >= st->max_slots) return 0;
    InstaCorrSlot *c = &st->slots[a->active_slot];
    if (c->corr_mode && c->corr_setup_done) {
        instacorr_compute(a, st, a->cx, a->cy, a->cz);
        return 1; /* consumed */
    }
    return 0;
}

static void ica_slice_override(App *a, int view, int coord, float *buf) {
    InstaCorrState *st = (InstaCorrState *)instacorr_plugin.ctx;
    if (!st) return;
    if (a->active_slot < 0 || a->active_slot >= st->max_slots) return;
    InstaCorrSlot *c = &st->slots[a->active_slot];
    if (!c->corr_mode || !c->corr_seed_valid) return;

    ImageSlot *cs = &CUR_SLOT(a);

    /* Ensure corr buffers are allocated (depends on slot dimensions) */
    int nx = cs->nx, ny = cs->ny, nz = cs->nz;
    if (!c->corr_axial_buf) {
        int max_xy = nx * ny;
        int max_xz = nx * nz;
        int max_yz = ny * nz;
        c->corr_axial_buf   = (float *)calloc((size_t)max_xy, sizeof(float));
        c->corr_sagittal_buf = (float *)calloc((size_t)max_yz, sizeof(float));
        c->corr_coronal_buf  = (float *)calloc((size_t)max_xz, sizeof(float));
        c->corr_zoom_axial_buf   = (float *)calloc((size_t)max_xy, sizeof(float));
        c->corr_zoom_sagittal_buf = (float *)calloc((size_t)max_yz, sizeof(float));
        c->corr_zoom_coronal_buf  = (float *)calloc((size_t)max_xz, sizeof(float));
        c->corr_seed_ts = (float *)calloc((size_t)cs->nt, sizeof(float));
    }

    int src_w, src_h;
    float *corr_buf;

    if (view == 0) {
        /* axial: coord = z */
        if (!c->corr_axial_buf) return;
        extract_corr_axial_slice(cs, c, coord);
        corr_buf = c->corr_axial_buf;
        src_w = nx; src_h = ny;
    } else if (view == 1) {
        /* sagittal: coord = x */
        if (!c->corr_sagittal_buf) return;
        extract_corr_sagittal_slice(cs, c, coord);
        corr_buf = c->corr_sagittal_buf;
        src_w = ny; src_h = nz;
    } else {
        /* coronal: coord = y */
        if (!c->corr_coronal_buf) return;
        extract_corr_coronal_slice(cs, c, coord);
        corr_buf = c->corr_coronal_buf;
        src_w = nx; src_h = nz;
    }

    /* Copy corr data into the main slice buffer */
    for (int row = 0; row < src_h; row++)
        memcpy(buf + (size_t)row * src_w,
               corr_buf + (size_t)row * src_w,
               (size_t)src_w * sizeof(float));
}

static void ica_render_overlay(App *a) {
    InstaCorrState *st = (InstaCorrState *)instacorr_plugin.ctx;
    if (!st) return;
    if (a->active_slot < 0 || a->active_slot >= st->max_slots) return;
    InstaCorrSlot *c = &st->slots[a->active_slot];
    if (!c->corr_mode || !c->corr_seed_valid) return;

    ImageSlot *cs = &CUR_SLOT(a);

    /* Draw threshold overlay using corr data in the main slice buffers,
     * which was filled by slice_override.  The draw_corr_overlay function
     * handles colormap and thresholding. */
    if (c->corr_axial_buf && a->axial_rect.x1 > a->axial_rect.x0)
        draw_corr_overlay(&a->plm_fb, a->axial_rect,
                          c->corr_axial_buf, cs->nx, cs->ny, c->corr_thresh);
    if (c->corr_sagittal_buf && a->sagittal_rect.x1 > a->sagittal_rect.x0)
        draw_corr_overlay(&a->plm_fb, a->sagittal_rect,
                          c->corr_sagittal_buf, cs->ny, cs->nz, c->corr_thresh);
    if (c->corr_coronal_buf && a->coronal_rect.x1 > a->coronal_rect.x0)
        draw_corr_overlay(&a->plm_fb, a->coronal_rect,
                          c->corr_coronal_buf, cs->nx, cs->nz, c->corr_thresh);
}

static void ica_shutdown(App *a) {
    (void)a;
    InstaCorrState *st = (InstaCorrState *)instacorr_plugin.ctx;
    if (st) {
        if (st->slots) {
            for (int i = 0; i < st->max_slots; i++) {
                InstaCorrSlot *c = &st->slots[i];
                free(c->corr_sum); free(c->corr_sum2); free(c->corr_std);
                free(c->corr_mask); free(c->corr_map); free(c->corr_idx);
                free(c->corr_vol_backup); free(c->corr_seed_ts); free(c->corr_std_dt);
                free(c->corr_axial_buf); free(c->corr_sagittal_buf); free(c->corr_coronal_buf);
                free(c->corr_zoom_axial_buf); free(c->corr_zoom_sagittal_buf); free(c->corr_zoom_coronal_buf);
            }
            free(st->slots);
        }
        free(st);
    }
    instacorr_plugin.ctx = NULL;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Plugin descriptor
 * ═══════════════════════════════════════════════════════════════════ */

Plugin instacorr_plugin = {
    .name           = "instacorr",
    .ctx            = NULL,
    .init           = ica_init,
    .keyboard       = ica_keyboard,
    .mouse_click    = ica_mouse_click,
    .slice_override = ica_slice_override,
    .render_overlay = ica_render_overlay,
    .shutdown       = ica_shutdown,
};
