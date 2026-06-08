#include <math.h>
#include "app.h"

static void extract_axial(float *dst, const float *vol,
                          int nx, int ny, int nz, int z) {
    if (z < 0) z = 0; if (z >= nz) z = nz - 1;
    size_t base = (size_t)z * nx * ny;
    for (int row = 0; row < ny; row++) {
        int src_y = ny - 1 - row;
        memcpy(dst + (size_t)row * nx, vol + base + (size_t)src_y * nx,
               (size_t)nx * sizeof(float));
    }
}

static void extract_sagittal(float *dst, const float *vol,
                             int nx, int ny, int nz, int x) {
    if (x < 0) x = 0; if (x >= nx) x = nx - 1;
    for (int row = 0; row < nz; row++) {
        int src_z = nz - 1 - row;
        for (int col = 0; col < ny; col++) {
            int src_y = ny - 1 - col;
            dst[(size_t)row * ny + col] =
                vol[(size_t)x + (size_t)src_y * nx + (size_t)src_z * nx * ny];
        }
    }
}

static void extract_coronal(float *dst, const float *vol,
                            int nx, int ny, int nz, int y) {
    if (y < 0) y = 0; if (y >= ny) y = ny - 1;
    for (int row = 0; row < nz; row++) {
        int src_z = nz - 1 - row;
        size_t base = (size_t)src_z * nx * ny + (size_t)y * nx;
        memcpy(dst + (size_t)row * nx, vol + base, (size_t)nx * sizeof(float));
    }
}

void extract_slices(App *app) {
    ImageSlot *cs = &app->slots[app->active_slot];
    int nx = cs->nx, ny = cs->ny, nz = cs->nz;
    int eff_cx = cs->cross_sync ? app->cx : cs->cx;
    int eff_cy = cs->cross_sync ? app->cy : cs->cy;
    int eff_cz = cs->cross_sync ? app->cz : cs->cz;

    /* 4D: offset into current timepoint */
    size_t toff = cs->nt > 1 ? (size_t)cs->ct * nx * ny * nz : 0;
    const float *vol = cs->vol + toff;

    extract_axial(app->axial_slice, vol, nx, ny, nz, eff_cz);
    extract_sagittal(app->sagittal_slice, vol, nx, ny, nz, eff_cx);
    extract_coronal(app->coronal_slice, vol, nx, ny, nz, eff_cy);
}

void extract_seg_slices(App *app) {
    ImageSlot *cs = &app->slots[app->active_slot];
    int eff_cx = cs->cross_sync ? app->cx : cs->cx;
    int eff_cy = cs->cross_sync ? app->cy : cs->cy;
    int eff_cz = cs->cross_sync ? app->cz : cs->cz;

    /* zero buffers first */
    memset(app->seg_axial_slice, 0, (size_t)cs->nx * cs->ny * sizeof(float));
    memset(app->seg_sagittal_slice, 0, (size_t)cs->ny * cs->nz * sizeof(float));
    memset(app->seg_coronal_slice, 0, (size_t)cs->nx * cs->nz * sizeof(float));

    /* extract each enabled seg */
    for (int si = 0; si < cs->num_segs; si++) {
        Attachment *seg = &cs->segs[si];
        if (!seg->enabled || !seg->vol) continue;
        int snx = seg->nx, sny = seg->ny, snz = seg->nz;
        float *sv = seg->vol;

        /* axial */
        { int z = eff_cz; z = (z * snz / cs->nz);
          if (z < 0) z = 0; if (z >= snz) z = snz - 1;
          size_t base = (size_t)z * snx * sny;
          for (int row = 0; row < sny && row < cs->ny; row++) {
            int src_y = sny - 1 - (row * sny / cs->ny);
            for (int col = 0; col < snx && col < cs->nx; col++) {
                int src_x = col * snx / cs->nx;
                float v = sv[base + (size_t)src_y * snx + src_x];
                if (v != 0.0f) app->seg_axial_slice[(size_t)row * cs->nx + col] = v;
            }
          }
        }
        /* sagittal */
        { int x = eff_cx; x = (x * snx / cs->nx);
          if (x < 0) x = 0; if (x >= snx) x = snx - 1;
          for (int row = 0; row < snz && row < cs->nz; row++) {
            int src_z = snz - 1 - (row * snz / cs->nz);
            for (int col = 0; col < sny && col < cs->ny; col++) {
                int src_y = sny - 1 - (col * sny / cs->ny);
                float v = sv[(size_t)x + (size_t)src_y * snx + (size_t)src_z * snx * sny];
                if (v != 0.0f) app->seg_sagittal_slice[(size_t)row * cs->ny + col] = v;
            }
          }
        }
        /* coronal */
        { int y = eff_cy; y = (y * sny / cs->ny);
          if (y < 0) y = 0; if (y >= sny) y = sny - 1;
          for (int row = 0; row < snz && row < cs->nz; row++) {
            int src_z = snz - 1 - (row * snz / cs->nz);
            for (int col = 0; col < snx && col < cs->nx; col++) {
                int src_x = col * snx / cs->nx;
                float v = sv[(size_t)src_z * snx * sny + (size_t)y * snx + src_x];
                if (v != 0.0f) app->seg_coronal_slice[(size_t)row * cs->nx + col] = v;
            }
          }
        }
    }
}

void extract_timeseries(App *app) {
    ImageSlot *cs = &app->slots[app->active_slot];
    if (cs->nt <= 1) return;
    if (!cs->ts_data) {
        cs->ts_data = malloc((size_t)cs->nt * sizeof(float));
        if (!cs->ts_data) { cs->ts_valid = 0; return; }
    }
    int eff_cx = cs->cross_sync ? app->cx : cs->cx;
    int eff_cy = cs->cross_sync ? app->cy : cs->cy;
    int eff_cz = cs->cross_sync ? app->cz : cs->cz;
    size_t nvox_t = (size_t)cs->nx * cs->ny * cs->nz;
    /* voxel index in storage order */
    size_t vox_off = (size_t)eff_cx + (size_t)eff_cy * cs->nx + (size_t)eff_cz * cs->nx * cs->ny;
    for (int t = 0; t < cs->nt; t++) {
        cs->ts_data[t] = cs->vol[(size_t)t * nvox_t + vox_off];
    }
    cs->ts_valid = 1;
    cs->ts_x = eff_cx; cs->ts_y = eff_cy; cs->ts_z = eff_cz;
}

void extract_ovl_slices(App *app) {
    ImageSlot *cs = &app->slots[app->active_slot];
    int eff_cx = cs->cross_sync ? app->cx : cs->cx;
    int eff_cy = cs->cross_sync ? app->cy : cs->cy;
    int eff_cz = cs->cross_sync ? app->cz : cs->cz;
    if (cs->num_ovls == 0 || !cs->ovls[0].enabled || !cs->ovls[0].vol) {
        memset(app->ovl_axial_slice, 0,
               (size_t)cs->nx * cs->ny * sizeof(float));
        memset(app->ovl_sagittal_slice, 0,
               (size_t)cs->ny * cs->nz * sizeof(float));
        memset(app->ovl_coronal_slice, 0,
               (size_t)cs->nx * cs->nz * sizeof(float));
        return;
    }

    /* zero first */
    memset(app->ovl_axial_slice, 0, (size_t)cs->nx * cs->ny * sizeof(float));
    memset(app->ovl_sagittal_slice, 0, (size_t)cs->ny * cs->nz * sizeof(float));
    memset(app->ovl_coronal_slice, 0, (size_t)cs->nx * cs->nz * sizeof(float));

    /* extract each enabled overlay */
    for (int oi = 0; oi < cs->num_ovls; oi++) {
        Attachment *ovl = &cs->ovls[oi];
        if (!ovl->enabled || !ovl->vol) continue;
        int onx = ovl->nx, ony = ovl->ny, onz = ovl->nz;
        float *ov = ovl->vol;

        /* axial */
        { int z = eff_cz; z = (z * onz / cs->nz);
          if (z < 0) z = 0; if (z >= onz) z = onz - 1;
          size_t base = (size_t)z * onx * ony;
          for (int row = 0; row < ony && row < cs->ny; row++) {
            int src_y = ony - 1 - (row * ony / cs->ny);
            for (int col = 0; col < onx && col < cs->nx; col++) {
                int src_x = col * onx / cs->nx;
                app->ovl_axial_slice[(size_t)row * cs->nx + col] =
                    ov[base + (size_t)src_y * onx + src_x];
            }
          }
        }
        /* sagittal */
        { int x = eff_cx; x = (x * onx / cs->nx);
          if (x < 0) x = 0; if (x >= onx) x = onx - 1;
          for (int row = 0; row < onz && row < cs->nz; row++) {
            int src_z = onz - 1 - (row * onz / cs->nz);
            for (int col = 0; col < ony && col < cs->ny; col++) {
                int src_y = ony - 1 - (col * ony / cs->ny);
                app->ovl_sagittal_slice[(size_t)row * cs->ny + col] =
                    ov[(size_t)x + (size_t)src_y * onx + (size_t)src_z * onx * ony];
            }
          }
        }
        /* coronal */
        { int y = eff_cy; y = (y * ony / cs->ny);
          if (y < 0) y = 0; if (y >= ony) y = ony - 1;
          for (int row = 0; row < onz && row < cs->nz; row++) {
            int src_z = onz - 1 - (row * onz / cs->nz);
            for (int col = 0; col < onx && col < cs->nx; col++) {
                int src_x = col * onx / cs->nx;
                app->ovl_coronal_slice[(size_t)row * cs->nx + col] =
                    ov[(size_t)src_z * onx * ony + (size_t)y * onx + src_x];
            }
          }
        }
    }
}
