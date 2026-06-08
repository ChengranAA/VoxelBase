#ifndef VOXELBASE_APP_H
#define VOXELBASE_APP_H

#include "nifti/nifti_image.h"
#include "raylib.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SLOTS 10
#define MAX_SEGS_PER_SLOT 3
#define MAX_OVLS_PER_SLOT 3

typedef struct {
  nifti_image *nim;
  float *vol;
  int nx, ny, nz;
  int enabled;
  float opacity;
  float threshold;
  uint32_t label_mask; /* bit N set = label N visible, default all on */
  int label_count;     /* number of unique labels found */
  int labels[20];      /* sorted unique label values */
  char filename[256];
} Attachment;

typedef struct {
  nifti_image *nim;
  float *vol;
  int nx, ny, nz, nt;
  double dx, dy, dz, tr;
  double vmin, vmax;
  double auto_vmin, auto_vmax;
  int cmap;

  /* 4D timeseries */
  int ct;
  float *ts_data;
  int ts_valid, ts_x, ts_y, ts_z;

  /* sidebar */
  char filename[256];
  Texture2D thumbnail;
  int thumb_z, thumb_ct, thumb_sz;
  double thumb_vmin, thumb_vmax;
  int thumb_cmap;

  /* attachments */
  Attachment segs[MAX_SEGS_PER_SLOT];
  int num_segs;
  Attachment ovls[MAX_OVLS_PER_SLOT];
  int num_ovls;

  double zoom;
  int zoom_sync; /* 1 = this slot follows global zoom */

  /* per-slot crosshair (when cross_sync is off) */
  int cx, cy, cz;
  int cross_sync; /* 1 = this slot follows global crosshair */
} ImageSlot;

typedef struct {
  int win_w, win_h;

  ImageSlot slots[MAX_SLOTS];
  int num_slots;
  int active_slot;

  /* crosshair */
  int cx, cy, cz;
  double ch_fx, ch_fy, ch_fz;
  int focus;

  /* viewport textures */
  Texture2D tex_axial, tex_sagittal, tex_coronal;

  /* slice buffers */
  float *axial_slice, *sagittal_slice, *coronal_slice;
  float *seg_axial_slice, *seg_sagittal_slice, *seg_coronal_slice;
  float *ovl_axial_slice, *ovl_sagittal_slice, *ovl_coronal_slice;

  /* rgba buffers for texture upload */
  uint8_t *rgba_axial, *rgba_sagittal, *rgba_coronal;

  /* global controls */
  double zoom;
  double seg_opacity;
  double ovl_thresh, ovl_opacity;
  float ovl_abs_max;

  /* sidebar */
  int sidebar_w;
  int sidebar_expanded;
  int sidebar_at_bottom;
  int sidebar_scroll_y, sidebar_scroll_x;
  int active_panel_tab;
  /* drag-to-reorder */
  int sidebar_state; /* 0=IDLE, 1=PENDING, 2=DRAGGING */
  int sidebar_drag_slot;
  int sidebar_drag_dst;
  int sidebar_drag_ox, sidebar_drag_oy;
  int sidebar_drag_mx, sidebar_drag_my;

  /* dirty flags */
  int dirty_slices;
  int dirty_contrast;
  int force_texture_recreate;

  int should_close;
  char out_dir[1024];

  int pending_attach; /* 0=none, 1=seg on next drop, 2=ovl on next drop */
  char drop_paths[10][1024];
  int drop_count;

  /* right-click context menu */
  int ctx_visible;
  int ctx_x, ctx_y;
  int ctx_is_seg, ctx_idx;

  int pending_remove_slot; /* -1=none, >=0 = slot to remove after drawing */

  /* sidebar context menu */
  int sctx_visible;
  int sctx_x, sctx_y, sctx_slot;
  int sctx_block_click; /* block next left-click in sidebar */

  /* REPL */
  char repl_input[256];
  int repl_cursor;
  char repl_output[200][128];
  int repl_out_count;
  int repl_scroll;
  char repl_pending_load[1024]; /* deferred load path */
  /* deferred in-memory slot creation (no tmp file) */
  float *repl_pending_vol;
  int repl_pending_nx, repl_pending_ny, repl_pending_nz, repl_pending_nt;
  double repl_pending_dx, repl_pending_dy, repl_pending_dz, repl_pending_tr;
  int repl_pending_has_data;
  char repl_hint[64]; /* autocompletion ghost text */
  int repl_mode;      /* 0=text output, 1=canvas */
/* plot data */
#define MAX_PLOT_SERIES 8
#define MAX_PLOT_POINTS 512
  struct {
    float xs[MAX_PLOT_POINTS], ys[MAX_PLOT_POINTS];
    int count;
    char name[32];
    Color color;
  } plot_series[MAX_PLOT_SERIES];
  int plot_series_count;
  int plot_hist[64];
  int plot_hist_count;
  float plot_hist_min, plot_hist_max;
  char plot_xlabel[32], plot_ylabel[32];
} App;

#endif
