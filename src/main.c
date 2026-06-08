#define RAYGUI_IMPLEMENTATION
#define NIFTI_BASE_IMPLEMENTATION
#define NIFTI_ZNZ_IMPLEMENTATION
#define NIFTI_HEADER_IMPLEMENTATION
#define NIFTI_IMAGE_IMPLEMENTATION

#include "raylib.h"
#include "raygui.h"
#include "cli/cli.h"
#include "app.h"
#include "plugins/vbl/vbl_bridge.h"

int  load_slots(App *app, CliArgs *args);
void app_init_buffers(App *app);
void extract_slices(App *app);
void render_and_upload(App *app, int force_recreate);
void extract_seg_slices(App *app);
void extract_ovl_slices(App *app);
void extract_timeseries(App *app);
void draw_viewport(App *app, int vp_id, Rectangle bounds);
void render_viewport(App *app, int vp_id, Rectangle cell, Rectangle *out_rect, Rectangle *out_nav);
void process_hotkeys(App *app);
void draw_panel(App *app, Rectangle bounds);
void draw_sidebar(App *app, Rectangle bounds);
int add_slot_from_file(App *app, const char *path);
int add_slot_from_pending(App *app);
int add_attachment_to_slot(App *app, int slot_idx, const char *path, int is_seg);
void save_screenshot(App *app);

/* ====================================================================
 *  app_init_buffers — allocate slice + rgba buffers for active slot
 * ==================================================================== */
void app_init_buffers(App *app) {
    /* find maximum dimensions across all loaded slots */
    int max_nx = 1, max_ny = 1, max_nz = 1;
    for (int i = 0; i < app->num_slots; i++) {
        if (app->slots[i].nx > max_nx) max_nx = app->slots[i].nx;
        if (app->slots[i].ny > max_ny) max_ny = app->slots[i].ny;
        if (app->slots[i].nz > max_nz) max_nz = app->slots[i].nz;
    }

    /* textures will be auto-recreated by upload_texture on dimension mismatch */

    /* initialize proportions on first load */
    if (app->ch_fx == 0.0 && app->ch_fy == 0.0 && app->ch_fz == 0.0 && app->num_slots > 0) {
        app->ch_fx = 0.5; app->ch_fy = 0.5; app->ch_fz = 0.5;
    }
    /* set crosshair to center of first slot if not already set */
    if (app->num_slots > 0 && app->cx == 0 && app->cy == 0 && app->cz == 0) {
        ImageSlot *s0 = &app->slots[0];
        app->cx = s0->nx / 2;
        app->cy = s0->ny / 2;
        app->cz = s0->nz / 2;
        app->ch_fx = 0.5; app->ch_fy = 0.5; app->ch_fz = 0.5;
    }
    /* always sync all synced slots and update global crosshair */
    for (int i = 0; i < app->num_slots; i++) {
        if (app->slots[i].cross_sync) {
            app->slots[i].cx = (int)(app->ch_fx * app->slots[i].nx + 0.5);
            app->slots[i].cy = (int)(app->ch_fy * app->slots[i].ny + 0.5);
            app->slots[i].cz = (int)(app->ch_fz * app->slots[i].nz + 0.5);
            if (app->slots[i].cx >= app->slots[i].nx) app->slots[i].cx = app->slots[i].nx - 1;
            if (app->slots[i].cy >= app->slots[i].ny) app->slots[i].cy = app->slots[i].ny - 1;
            if (app->slots[i].cz >= app->slots[i].nz) app->slots[i].cz = app->slots[i].nz - 1;
            if (app->slots[i].cx < 0) app->slots[i].cx = 0;
            if (app->slots[i].cy < 0) app->slots[i].cy = 0;
            if (app->slots[i].cz < 0) app->slots[i].cz = 0;
        }
    }
    /* update global crosshair to active slot's proportional position */
    ImageSlot *as = &app->slots[app->active_slot];
    if (as->cross_sync) {
        app->cx = as->cx; app->cy = as->cy; app->cz = as->cz;
    }

    free(app->axial_slice);    free(app->sagittal_slice);    free(app->coronal_slice);
    free(app->seg_axial_slice); free(app->seg_sagittal_slice); free(app->seg_coronal_slice);
    free(app->ovl_axial_slice); free(app->ovl_sagittal_slice); free(app->ovl_coronal_slice);
    free(app->rgba_axial);     free(app->rgba_sagittal);     free(app->rgba_coronal);

    /* axial:  max_nx × max_ny,  sagittal: max_ny × max_nz,  coronal: max_nx × max_nz */
    app->axial_slice    = malloc((size_t)max_nx * max_ny * sizeof(float));
    app->sagittal_slice = malloc((size_t)max_ny * max_nz * sizeof(float));
    app->coronal_slice  = malloc((size_t)max_nx * max_nz * sizeof(float));

    app->seg_axial_slice    = calloc((size_t)max_nx * max_ny, sizeof(float));
    app->seg_sagittal_slice = calloc((size_t)max_ny * max_nz, sizeof(float));
    app->seg_coronal_slice  = calloc((size_t)max_nx * max_nz, sizeof(float));

    app->ovl_axial_slice    = calloc((size_t)max_nx * max_ny, sizeof(float));
    app->ovl_sagittal_slice = calloc((size_t)max_ny * max_nz, sizeof(float));
    app->ovl_coronal_slice  = calloc((size_t)max_nx * max_nz, sizeof(float));

    app->rgba_axial    = malloc((size_t)max_nx * max_ny * 4);
    app->rgba_sagittal = malloc((size_t)max_ny * max_nz * 4);
    app->rgba_coronal  = malloc((size_t)max_nx * max_nz * 4);
}

/* ====================================================================
 *  viewport_mouse_to_voxel — convert pixel coords → voxel coords
 *     respecting the fit_into aspect-ratio transform
 * ==================================================================== */
static int map_to_voxel(Rectangle r, int img_w, int img_h,
                         int mx, int my, int *vx, int *vy) {
    float aspect = (float)img_w / (float)img_h;
    float cell_aspect = r.width / r.height;
    Rectangle fit = r;

    if (cell_aspect > aspect) {
        fit.width = r.height * aspect;
        fit.x += (r.width - fit.width) / 2.0f;
    } else {
        fit.height = r.width / aspect;
        fit.y += (r.height - fit.height) / 2.0f;
    }
    if (mx < fit.x || mx >= fit.x + fit.width ||
        my < fit.y || my >= fit.y + fit.height) return 0;

    float fx = (mx - fit.x) / fit.width;
    float fy = (my - fit.y) / fit.height;
    *vx = (int)(fx * img_w);
    *vy = (int)(fy * img_h);
    if (*vx < 0) *vx = 0; if (*vx >= img_w) *vx = img_w - 1;
    if (*vy < 0) *vy = 0; if (*vy >= img_h) *vy = img_h - 1;
    return 1;
}

/* ====================================================================
 *  render_viewport — draw a single MRI view into a grid cell
 *     (with background, border, label, crosshair, focus border, zoom)
 *     out_rect = image fit rect, out_nav = nav preview rect (or {0} if no zoom)
 * ==================================================================== */
void render_viewport(App *app, int vp_id, Rectangle cell,
                     Rectangle *out_rect, Rectangle *out_nav) {
    ImageSlot *cs = &app->slots[app->active_slot];
    int eff_cx = cs->cross_sync ? app->cx : cs->cx;
    int eff_cy = cs->cross_sync ? app->cy : cs->cy;
    int eff_cz = cs->cross_sync ? app->cz : cs->cz;
    int img_w, img_h, ch_x_px, ch_y_px;
    const char *label;
    Texture2D tex;
    Color border_col = (app->focus == vp_id) ? (Color){0,200,255,255} : (Color){60,60,60,255};

    switch (vp_id) {
    case 0: img_w = cs->nx; img_h = cs->ny; tex = app->tex_axial;
            ch_x_px = eff_cx; ch_y_px = cs->ny - 1 - eff_cy;
            label = "AXIAL  (XY)"; break;
    case 1: img_w = cs->ny; img_h = cs->nz; tex = app->tex_sagittal;
            ch_x_px = cs->ny - 1 - eff_cy; ch_y_px = cs->nz - 1 - eff_cz;
            label = "SAGITTAL  (YZ)"; break;
    case 2: img_w = cs->nx; img_h = cs->nz; tex = app->tex_coronal;
            ch_x_px = eff_cx; ch_y_px = cs->nz - 1 - eff_cz;
            label = "CORONAL  (XZ)"; break;
    default: return;
    }

    *out_nav = (Rectangle){0,0,0,0};

    /* background fill */
    DrawRectangleRec(cell, (Color){20,20,20,255});

    /* 1px border (focus = brighter) */
    DrawRectangleLinesEx((Rectangle){cell.x, cell.y, cell.width, cell.height},
                         1, border_col);

    /* fit image in cell with 2px border inset, reserve 18px top for label */
    Rectangle inner = {cell.x + 2, cell.y + 20, cell.width - 4, cell.height - 22};
    float aspect = (float)img_w / (float)img_h;
    float cell_aspect = inner.width / inner.height;
    Rectangle fit = inner;

    if (cell_aspect > aspect) {
        fit.width = inner.height * aspect;
        fit.x += (inner.width - fit.width) / 2.0f;
    } else {
        fit.height = inner.width / aspect;
        fit.y += (inner.height - fit.height) / 2.0f;
    }

    *out_rect = fit;

    /* pixel-align the fit rect for consistent image+crosshair positioning */
    fit.x = (float)(int)(fit.x + 0.5f);
    fit.y = (float)(int)(fit.y + 0.5f);
    fit.width = (float)(int)(fit.width + 0.5f);
    fit.height = (float)(int)(fit.height + 0.5f);

    double zoom_level = cs->zoom_sync ? app->zoom : cs->zoom;
    int sx = 0, sy = 0, sub_w = img_w, sub_h = img_h;

    if (tex.id > 0) {
        if (zoom_level > 1.01) {
            /* zoomed: crop subregion around crosshair */
            sub_w = (int)(img_w / zoom_level); sub_h = (int)(img_h / zoom_level);
            if (sub_w < 1) sub_w = 1; if (sub_h < 1) sub_h = 1;
            sx = ch_x_px - sub_w/2; sy = ch_y_px - sub_h/2;
            if (sx < 0) sx = 0; if (sy < 0) sy = 0;
            if (sx + sub_w > img_w) sx = img_w - sub_w;
            if (sy + sub_h > img_h) sy = img_h - sub_h;

            DrawTexturePro(tex, (Rectangle){sx, sy, sub_w, sub_h}, fit,
                           (Vector2){0,0}, 0, WHITE);

            /* green zoom border */
            DrawRectangleLinesEx(fit, 2, (Color){0,255,0,255});

            /* nav preview (small overview in corner) */
            int pvw = (int)(cell.width * 0.22f), pvh = (int)(cell.height * 0.22f);
            if (pvw > 80) pvw = 80; if (pvh > 80) pvh = 80;
            if (pvw > 24 && pvh > 24) {
                Rectangle pv = {cell.x + cell.width - pvw - 8, cell.y + cell.height - pvh - 8, pvw, pvh};
                float pa = (float)img_w / img_h, ca = pv.width / pv.height;
                Rectangle pv_fit = pv;
                if (ca > pa) {
                    pv_fit.width = pv.height * pa;
                    pv_fit.x += (pv.width - pv_fit.width) / 2.0f;
                } else {
                    pv_fit.height = pv.width / pa;
                    pv_fit.y += (pv.height - pv_fit.height) / 2.0f;
                }
                DrawTexturePro(tex, (Rectangle){0,0,img_w,img_h}, pv_fit,
                               (Vector2){0,0}, 0, WHITE);

                *out_nav = pv_fit;  /* for nav-drag hit testing */

                /* zoom rect on nav preview */
                Rectangle zb = {
                    pv_fit.x + (float)sx / img_w * pv_fit.width,
                    pv_fit.y + (float)sy / img_h * pv_fit.height,
                    (float)sub_w / img_w * pv_fit.width,
                    (float)sub_h / img_h * pv_fit.height
                };
                DrawRectangleLinesEx(zb, 1, GREEN);
                DrawRectangleLinesEx(pv_fit, 1, (Color){100,100,100,255});
            }
        } else {
            DrawTexturePro(tex, (Rectangle){0,0,img_w,img_h}, fit,
                           (Vector2){0,0}, 0, WHITE);
        }
    }

    /* label — centered at top of cell above image */
    int lw = MeasureText(label, 10);
    DrawText(label, (int)(cell.x + (cell.width - lw) / 2), (int)cell.y + 4, 10, (Color){180,180,180,255});

    /* crosshair — fit rect is pixel-aligned, use integer math */
    int rx = (int)fit.x, ry = (int)fit.y;
    int rw = (int)fit.width, rh = (int)fit.height;
    int ch_x = rx + (((int)ch_x_px - sx) * rw + rw / 2) / sub_w;
    int ch_y = ry + (((int)ch_y_px - sy) * rh + rh / 2) / sub_h;
    DrawLine(ch_x, ry, ch_x, ry + rh, GREEN);
    DrawLine(rx, ch_y, rx + rw, ch_y, GREEN);

    /* focus border */
    if (app->focus == vp_id) {
        Rectangle fb = {cell.x+2, cell.y+2, cell.width-4, cell.height-4};
        DrawRectangleLinesEx(fb, 2, (Color){0,200,255,255});
    }
}

/* ====================================================================
 *  draw_viewport — raygui panel wrapper around render_viewport
 * ==================================================================== */
void draw_viewport(App *app, int vp_id, Rectangle bounds) {
    (void)bounds; /* unused — render_viewport handles everything */
}

/* ====================================================================
 *  handle_viewport_click — map pixel coords to world coords
 * ==================================================================== */
static void handle_viewport_click(App *app, int vp_id, Rectangle cell,
                                   int mx, int my) {
    ImageSlot *cs = &app->slots[app->active_slot];
    int img_w, img_h;
    switch (vp_id) {
    case 0: img_w = cs->nx; img_h = cs->ny; break;
    case 1: img_w = cs->ny; img_h = cs->nz; break;
    case 2: img_w = cs->nx; img_h = cs->nz; break;
    default: return;
    }

    /* reconstruct fit rect (same as render_viewport) */
    Rectangle inner = {cell.x + 2, cell.y + 20, cell.width - 4, cell.height - 22};
    float aspect = (float)img_w / (float)img_h;
    float cell_aspect = inner.width / inner.height;
    Rectangle fit = inner;
    if (cell_aspect > aspect) {
        fit.width = inner.height * aspect;
        fit.x += (inner.width - fit.width) / 2.0f;
    } else {
        fit.height = inner.width / aspect;
        fit.y += (inner.height - fit.height) / 2.0f;
    }

    int vx, vy;
    if (!map_to_voxel(fit, img_w, img_h, mx, my, &vx, &vy)) return;

    app->focus = vp_id;

    int nx, ny, nz;  /* new crosshair values */
    switch (vp_id) {
    case 0: nx = vx;                      ny = cs->ny - 1 - vy; nz = app->cz; break;
    case 1: nx = app->cx;                 ny = cs->ny - 1 - vx; nz = cs->nz - 1 - vy; break;
    case 2: nx = vx;                      ny = app->cy;         nz = cs->nz - 1 - vy; break;
    default: return;
    }

    if (nx < 0) nx = 0; if (nx >= cs->nx) nx = cs->nx - 1;
    if (ny < 0) ny = 0; if (ny >= cs->ny) ny = cs->ny - 1;
    if (nz < 0) nz = 0; if (nz >= cs->nz) nz = cs->nz - 1;

    if (cs->cross_sync) {
        /* synced: update global + proportions, propagate to all synced slots */
        app->cx = nx; app->cy = ny; app->cz = nz;
        app->ch_fx = cs->nx > 1 ? (double)nx / cs->nx : 0.5;
        app->ch_fy = cs->ny > 1 ? (double)ny / cs->ny : 0.5;
        app->ch_fz = cs->nz > 1 ? (double)nz / cs->nz : 0.5;
        for (int i = 0; i < app->num_slots; i++)
            if (app->slots[i].cross_sync) {
                app->slots[i].cx = (int)(app->ch_fx * app->slots[i].nx + 0.5);
                app->slots[i].cy = (int)(app->ch_fy * app->slots[i].ny + 0.5);
                app->slots[i].cz = (int)(app->ch_fz * app->slots[i].nz + 0.5);
            }
    } else {
        /* per-slot: modify only this slot, preserve global crosshair */
        cs->cx = nx; cs->cy = ny; cs->cz = nz;
    }
    app->dirty_slices = 1;
}

/* ====================================================================
 *  main
 * ==================================================================== */
int main(int argc, char **argv) {
    CliArgs args;
    int ret = cli_parse(argc, argv, &args);
    if (ret == 2) return 0;
    if (ret != 0) return 1;

    App app = {0};
    app.win_w = args.win_w;
    app.win_h = args.win_h;
    app.sidebar_expanded = 0;
    app.sidebar_w = 100;
    app.sidebar_at_bottom = 0;
    app.sidebar_scroll_y = 0;
    app.sidebar_scroll_x = 0;
    app.active_panel_tab = 0;
    app.zoom = 1.0;
    app.seg_opacity = 0.5;
    app.ovl_opacity = 0.5;
    app.ovl_thresh = 0.0;
    app.pending_remove_slot = -1;
    app.dirty_slices = 1; /* ensure first frame renders */
    if (args.out_dir) snprintf(app.out_dir, sizeof(app.out_dir), "%s", args.out_dir);

    if (load_slots(&app, &args) != 0) {
        /* empty start — ok, user can drag-drop */
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(app.win_w, app.win_h, "VoxelBase");
    SetWindowMinSize(600, 400);
    SetTargetFPS(60);
    SetExitKey(0);
    SetTraceLogLevel(LOG_ERROR); /* suppress raylib INFO spam */
    GuiLoadStyleDefault();

    /* dark theme */
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1e1e2eff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x444455ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x2a2a3aff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xc0c0d0ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xe0e0f0ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x5599ccff);
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, 0xc0c0d0ff);
    GuiSetStyle(TOGGLE, BASE_COLOR_NORMAL, 0x2a2a3aff);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x3a4a5aff);

    if (load_slots(&app, &args) != 0) {
        /* empty start — ok, user can drag-drop */
    }
    if (app.num_slots > 0) {
        app_init_buffers(&app);
        extract_slices(&app);
        render_and_upload(&app, 1);
    }

    /* init VBL bridge */
    vbl_bridge_init(&app);

    /* viewport rect tracking for mouse input */
    Rectangle cell_axial, cell_sagittal, cell_coronal, cell_panel;
    Rectangle fit_axial, fit_sag, fit_cor;
    Rectangle nav_axial, nav_sag, nav_cor;
    int mouse_down = 0;

    while (!WindowShouldClose() && !app.should_close) {
        /* always track window size */
        app.win_w = GetScreenWidth();
        app.win_h = GetScreenHeight();

        /* drag-and-drop — buffer paths */
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            app.drop_count = 0;
            for (unsigned int i = 0; i < dropped.count && app.drop_count < 10; i++) {
                const char *path = dropped.paths[i];
                int len = (int)strlen(path);
                if ((len > 4 && strcmp(path + len - 4, ".nii") == 0) ||
                    (len > 7 && strcmp(path + len - 7, ".nii.gz") == 0)) {
                    snprintf(app.drop_paths[app.drop_count], sizeof(app.drop_paths[0]), "%s", path);
                    app.drop_count++;
                }
            }
            UnloadDroppedFiles(dropped);
        }
        /* process new-slot drops immediately if no pending attachment */
        if (app.drop_count > 0 && !app.pending_attach) {
            int prev_count = app.num_slots;
            for (int i = 0; i < app.drop_count && app.num_slots < MAX_SLOTS; i++)
                add_slot_from_file(&app, app.drop_paths[i]);
            app.drop_count = 0;
            if (app.num_slots > prev_count) {
                app.active_slot = app.num_slots - 1;
                app.active_panel_tab = 0;
                app_init_buffers(&app);
                app.force_texture_recreate = 1;
                app.dirty_slices = 1;
            }
        }

        process_hotkeys(&app);

        /* mouse drag on viewports */
        Vector2 mouse = GetMousePosition();
        int mx = (int)mouse.x, my = (int)mouse.y;

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            mouse_down = 1;
            /* clear context menu click block on any press */
            if (!app.sctx_visible) app.sctx_block_click = 0;
            /* sidebar divider click to toggle expand/collapse */
            if (app.num_slots > 1 && mx >= app.sidebar_w - 12 && mx < app.sidebar_w) {
                app.sidebar_expanded = !app.sidebar_expanded;
                app.sidebar_w = app.sidebar_expanded ? 240 : 100;
                app.sidebar_scroll_y = 0;
            }
            /* sidebar drag: start PENDING */
            if (app.num_slots > 1 && mx < app.sidebar_w - 12 && !app.sidebar_at_bottom
                && !app.sctx_visible && !app.sctx_block_click) {
                int exp = app.sidebar_expanded;
                int thumb_sz = exp ? 160 : 64;
                int line_h = 14, text_lines = exp ? 3 : 1;
                int row_h = thumb_sz + text_lines * (line_h + 2) + 8;
                int rel_y = my - 8 + app.sidebar_scroll_y;
                int slot = rel_y / row_h;
                if (slot >= 0 && slot < app.num_slots) {
                    app.sidebar_state = 1; /* PENDING */
                    app.sidebar_drag_slot = slot;
                    app.sidebar_drag_dst = slot;
                    app.sidebar_drag_ox = mx;
                    app.sidebar_drag_oy = my;
                }
            }
        } else if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            mouse_down = 0;
            /* sidebar drag: RELEASE */
            if (app.sidebar_state == 2) { /* DRAGGING → commit reorder */
                int src = app.sidebar_drag_slot, dst = app.sidebar_drag_dst;
                if (src != dst && dst >= 0 && dst <= app.num_slots) {
                    ImageSlot moved = app.slots[src];
                    if (dst < src) {
                        memmove(&app.slots[dst + 1], &app.slots[dst],
                                (size_t)(src - dst) * sizeof(ImageSlot));
                        app.slots[dst] = moved;
                        app.active_slot = dst;
                    } else {
                        memmove(&app.slots[src], &app.slots[src + 1],
                                (size_t)(dst - src - 1) * sizeof(ImageSlot));
                        app.slots[dst - 1] = moved;
                        app.active_slot = dst - 1;
                    }
                    /* sync crosshair to new active slot */
                    ImageSlot *ns = &app.slots[app.active_slot];
                    if (ns->cross_sync) {
                        app.cx = (int)(app.ch_fx * ns->nx + 0.5);
                        app.cy = (int)(app.ch_fy * ns->ny + 0.5);
                        app.cz = (int)(app.ch_fz * ns->nz + 0.5);
                        if (app.cx >= ns->nx) app.cx = ns->nx - 1;
                        if (app.cy >= ns->ny) app.cy = ns->ny - 1;
                        if (app.cz >= ns->nz) app.cz = ns->nz - 1;
                        if (app.cx < 0) app.cx = 0;
                        if (app.cy < 0) app.cy = 0;
                        if (app.cz < 0) app.cz = 0;
                    }
                    app.force_texture_recreate = 1;
                    app.dirty_slices = 1;
                }
                app.sidebar_state = 0;
            } else if (app.sidebar_state == 1) { /* PENDING → select slot (no drag) */
                int old_slot = app.active_slot;
                int slot = app.sidebar_drag_slot;
                if (slot != old_slot) {
                    app.active_slot = slot;
                    ImageSlot *ns = &app.slots[slot];
                    if (ns->cross_sync) {
                        app.cx = (int)(app.ch_fx * ns->nx + 0.5);
                        app.cy = (int)(app.ch_fy * ns->ny + 0.5);
                        app.cz = (int)(app.ch_fz * ns->nz + 0.5);
                        if (app.cx >= ns->nx) app.cx = ns->nx - 1;
                        if (app.cy >= ns->ny) app.cy = ns->ny - 1;
                        if (app.cz >= ns->nz) app.cz = ns->nz - 1;
                        if (app.cx < 0) app.cx = 0;
                        if (app.cy < 0) app.cy = 0;
                        if (app.cz < 0) app.cz = 0;
                    }
                    app.force_texture_recreate = 1;
                    app.dirty_slices = 1;
                }
                app.sidebar_state = 0;
            }
        }

        /* sidebar drag: MOVE */
        if (app.sidebar_state == 1) {
            int dx = mx - app.sidebar_drag_ox, dy = my - app.sidebar_drag_oy;
            if (dx * dx + dy * dy >= 100) {
                app.sidebar_state = 2; /* → DRAGGING */
                app.sidebar_drag_mx = mx;
                app.sidebar_drag_my = my;
            }
        }
        if (app.sidebar_state == 2) {
            app.sidebar_drag_mx = mx;
            app.sidebar_drag_my = my;
            int exp = app.sidebar_expanded;
            int thumb_sz = exp ? 160 : 64;
            int row_h = thumb_sz + (exp ? 3 : 1) * 16 + 8;
            int rel_y = my - 8 + app.sidebar_scroll_y;
            int dst = (rel_y + row_h / 2) / row_h;
            if (dst < 0) dst = 0;
            if (dst > app.num_slots) dst = app.num_slots;
            app.sidebar_drag_dst = dst;

            /* auto-scroll when dragging near top/bottom edge */
            int edge_zone = 48;
            int total_h = 8 + app.num_slots * row_h;
            int max_scroll = total_h - app.win_h;
            if (max_scroll < 0) max_scroll = 0;
            if (my < edge_zone) {
                int speed = (edge_zone - my) / 4 + 1;
                app.sidebar_scroll_y -= speed;
                if (app.sidebar_scroll_y < 0) app.sidebar_scroll_y = 0;
            } else if (my > app.win_h - edge_zone) {
                int speed = (my - (app.win_h - edge_zone)) / 4 + 1;
                app.sidebar_scroll_y += speed;
                if (app.sidebar_scroll_y > max_scroll) app.sidebar_scroll_y = max_scroll;
            }

            /* skip viewport input while dragging */
            mouse_down = 0;
        }

        /* viewport mouse: nav drag + click */
        if (app.num_slots > 0 && mouse_down && app.sidebar_state == 0) {
            int nav_hit = 0;
        }

        /* chart click-scrub: click on timeseries to set timepoint */
        if (app.num_slots > 0 && mouse_down && app.active_panel_tab == 1) {
            ImageSlot *cs_ts = &app.slots[app.active_slot];
            if (cs_ts->nt > 1 && cs_ts->ts_valid) {
                /* chart is at bottom of panel: anchored to panel inner bottom */
                int chart_x = (int)cell_panel.x + 12;
                int chart_w = (int)cell_panel.width - 24;
                int chart_h = 90;
                int chart_y = (int)cell_panel.y + (int)cell_panel.height - 22 - 10 - chart_h;
                if (mx >= chart_x && mx < chart_x + chart_w &&
                    my >= chart_y && my < chart_y + chart_h) {
                    float frac = (float)(mx - chart_x) / (float)chart_w;
                    int new_ct = (int)(frac * (cs_ts->nt - 1) + 0.5f);
                    if (new_ct < 0) new_ct = 0;
                    if (new_ct >= cs_ts->nt) new_ct = cs_ts->nt - 1;
                    if (new_ct != cs_ts->ct) {
                        cs_ts->ct = new_ct;
                        app.dirty_slices = 1;
                    }
                }
            }
        }

        /* ── rebuild viewport data every frame (skip if unchanged) ── */
        if (app.num_slots > 0) {
            if (app.dirty_slices || app.dirty_contrast) {
                extract_slices(&app);
                extract_seg_slices(&app);
                extract_ovl_slices(&app);
                render_and_upload(&app, app.force_texture_recreate);
                app.force_texture_recreate = 0;
                app.dirty_slices = 0;
                app.dirty_contrast = 0;
            }
            if (app.active_panel_tab == 1)
                extract_timeseries(&app);
        }

        BeginDrawing();
        ClearBackground((Color){30,30,30,255});

        /* ── layout: compute cell rects ── */
        {
            int sbw = app.num_slots > 1 ? app.sidebar_w : 0;
            int main_x = sbw, main_w = app.win_w - main_x;
            int col_w = main_w / 2, row_h = app.win_h / 2;

            cell_axial    = (Rectangle){main_x,          0,        col_w, row_h};
            cell_sagittal = (Rectangle){main_x + col_w,  0,        col_w, row_h};
            cell_coronal  = (Rectangle){main_x,          row_h,    col_w, row_h};

            if (app.num_slots > 0) {
                render_viewport(&app, 0, cell_axial,    &fit_axial, &nav_axial);
                render_viewport(&app, 1, cell_sagittal, &fit_sag,   &nav_sag);
                render_viewport(&app, 2, cell_coronal,  &fit_cor,   &nav_cor);
            }
        }

        if (app.num_slots > 0 && mouse_down) {
            /* nav preview drag (zoomed only) */
            ImageSlot *cs = &app.slots[app.active_slot];
            double eff_zoom = cs->zoom_sync ? app.zoom : cs->zoom;
            int nav_hit = 0;
            if (eff_zoom > 1.01) {
                int new_cx = app.cx, new_cy = app.cy, new_cz = app.cz;
                if (nav_axial.width > 0 && CheckCollisionPointRec(mouse, nav_axial)) {
                    nav_hit = 1;
                    float fx = (mx - nav_axial.x) / nav_axial.width;
                    float fy = (my - nav_axial.y) / nav_axial.height;
                    if (fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1) {
                        new_cx = (int)(fx * cs->nx);
                        new_cy = cs->ny - 1 - (int)(fy * cs->ny);
                        if (new_cx >= cs->nx) new_cx = cs->nx - 1;
                        if (new_cy >= cs->ny) new_cy = cs->ny - 1;
                        if (new_cx < 0) new_cx = 0;
                        if (new_cy < 0) new_cy = 0;
                        app.focus = 0;
                    }
                } else if (nav_sag.width > 0 && CheckCollisionPointRec(mouse, nav_sag)) {
                    nav_hit = 1;
                    float fx = (mx - nav_sag.x) / nav_sag.width;
                    float fy = (my - nav_sag.y) / nav_sag.height;
                    if (fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1) {
                        new_cy = cs->ny - 1 - (int)(fx * cs->ny);
                        new_cz = cs->nz - 1 - (int)(fy * cs->nz);
                        if (new_cy >= cs->ny) new_cy = cs->ny - 1;
                        if (new_cz >= cs->nz) new_cz = cs->nz - 1;
                        if (new_cy < 0) new_cy = 0;
                        if (new_cz < 0) new_cz = 0;
                        app.focus = 1;
                    }
                } else if (nav_cor.width > 0 && CheckCollisionPointRec(mouse, nav_cor)) {
                    nav_hit = 1;
                    float fx = (mx - nav_cor.x) / nav_cor.width;
                    float fy = (my - nav_cor.y) / nav_cor.height;
                    if (fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1) {
                        new_cx = (int)(fx * cs->nx);
                        new_cz = cs->nz - 1 - (int)(fy * cs->nz);
                        if (new_cx >= cs->nx) new_cx = cs->nx - 1;
                        if (new_cz >= cs->nz) new_cz = cs->nz - 1;
                        if (new_cx < 0) new_cx = 0;
                        if (new_cz < 0) new_cz = 0;
                        app.focus = 2;
                    }
                }
                if (nav_hit) {
                    if (cs->cross_sync) {
                        app.cx = new_cx; app.cy = new_cy; app.cz = new_cz;
                        app.ch_fx = cs->nx > 1 ? (double)new_cx / cs->nx : 0.5;
                        app.ch_fy = cs->ny > 1 ? (double)new_cy / cs->ny : 0.5;
                        app.ch_fz = cs->nz > 1 ? (double)new_cz / cs->nz : 0.5;
                        for (int i = 0; i < app.num_slots; i++)
                            if (app.slots[i].cross_sync) {
                                app.slots[i].cx = (int)(app.ch_fx * app.slots[i].nx + 0.5);
                                app.slots[i].cy = (int)(app.ch_fy * app.slots[i].ny + 0.5);
                                app.slots[i].cz = (int)(app.ch_fz * app.slots[i].nz + 0.5);
                            }
                    } else {
                        cs->cx = new_cx; cs->cy = new_cy; cs->cz = new_cz;
                    }
                    app.dirty_slices = 1;
                }
            }
            if (!nav_hit) {
                if (CheckCollisionPointRec(mouse, cell_axial))
                    handle_viewport_click(&app, 0, cell_axial, mx, my);
                else if (CheckCollisionPointRec(mouse, cell_sagittal))
                    handle_viewport_click(&app, 1, cell_sagittal, mx, my);
                else if (CheckCollisionPointRec(mouse, cell_coronal))
                    handle_viewport_click(&app, 2, cell_coronal, mx, my);
            }
        }

        /* ── empty state: full-window REPL ── */
        if (app.num_slots == 0) {
            Rectangle repl_bounds = {0, 0, (float)app.win_w, (float)app.win_h};
            draw_panel(&app, repl_bounds);
            EndDrawing();
            /* still process REPL deferred load */
            if (app.repl_pending_has_data && app.num_slots < MAX_SLOTS) {
                int idx = add_slot_from_pending(&app);
                if (idx >= 0) {
                    app.active_slot = idx;
                    app.active_panel_tab = 0;
                    app_init_buffers(&app);
                    app.dirty_slices = 1;
                    app.force_texture_recreate = 1;
                }
            }
            if (app.repl_pending_load[0] && app.num_slots < MAX_SLOTS) {
                int idx = add_slot_from_file(&app, app.repl_pending_load);
                if (idx >= 0) {
                    app.active_slot = idx;
                    app_init_buffers(&app);
                    app.dirty_slices = 1;
                    app.force_texture_recreate = 1;
                    if (app.repl_out_count < 200)
                        snprintf(app.repl_output[app.repl_out_count++], 128, "  loaded slot %d", idx);
                }
                app.repl_pending_load[0] = 0;
    app.repl_pending_vol = NULL;
    app.repl_pending_has_data = 0;
            }
            continue;
        }

        /* ── 2×2 equal grid ── (already drawn above, just draw panel) */
        cell_panel = (Rectangle){cell_sagittal.x, cell_coronal.y, cell_coronal.width, cell_sagittal.height};
        draw_panel(&app, cell_panel);

        /* ── sidebar (drawn last to avoid raygui state conflicts) ── */
        {
            int sbw = app.num_slots > 1 ? app.sidebar_w : 0;
            if (sbw > 0) {
                draw_sidebar(&app, (Rectangle){0, 0, sbw, (float)app.win_h});
            }
        }

        EndDrawing();

        /* process pending slot removal */
        if (app.pending_remove_slot >= 0 && app.pending_remove_slot < app.num_slots) {
            int s = app.pending_remove_slot;
            nifti_image_free(app.slots[s].nim);
            free(app.slots[s].vol);
            if (app.slots[s].thumbnail.id > 0) UnloadTexture(app.slots[s].thumbnail);
            free(app.slots[s].ts_data);
            /* free attachments */
            for (int ai = 0; ai < app.slots[s].num_segs; ai++) {
                nifti_image_free(app.slots[s].segs[ai].nim);
                free(app.slots[s].segs[ai].vol);
            }
            for (int ai = 0; ai < app.slots[s].num_ovls; ai++) {
                nifti_image_free(app.slots[s].ovls[ai].nim);
                free(app.slots[s].ovls[ai].vol);
            }
            for (int j = s; j < app.num_slots - 1; j++) app.slots[j] = app.slots[j + 1];
            app.num_slots--;
            memset(&app.slots[app.num_slots], 0, sizeof(ImageSlot));
            if (app.num_slots == 0) {
                app.active_slot = 0;
            } else {
                if (app.active_slot >= app.num_slots) app.active_slot = app.num_slots - 1;
                ImageSlot *ns = &app.slots[app.active_slot];
                if (ns->cross_sync) {
                    app.cx = (int)(app.ch_fx * ns->nx + 0.5);
                    app.cy = (int)(app.ch_fy * ns->ny + 0.5);
                    app.cz = (int)(app.ch_fz * ns->nz + 0.5);
                } else {
                    app.cx = ns->cx; app.cy = ns->cy; app.cz = ns->cz;
                }
                if (app.cx >= ns->nx) app.cx = ns->nx - 1;
                if (app.cy >= ns->ny) app.cy = ns->ny - 1;
                if (app.cz >= ns->nz) app.cz = ns->nz - 1;
                if (app.cx < 0) app.cx = 0;
                if (app.cy < 0) app.cy = 0;
                if (app.cz < 0) app.cz = 0;
                app_init_buffers(&app);
                app.force_texture_recreate = 1;
                app.dirty_slices = 1;
            }
            app.pending_remove_slot = -1;
        }

        /* process REPL deferred load (in-memory or file) */
        if (app.repl_pending_has_data && app.num_slots < MAX_SLOTS) {
            int idx = add_slot_from_pending(&app);
            if (idx >= 0) {
                app.active_slot = idx;
                app.active_panel_tab = 0;
                app_init_buffers(&app);
                app.force_texture_recreate = 1;
                app.dirty_slices = 1;
            }
        }
        if (app.repl_pending_load[0] && app.num_slots < MAX_SLOTS) {
            int idx = add_slot_from_file(&app, app.repl_pending_load);
            if (idx >= 0) {
                app.active_slot = idx;
                app.active_panel_tab = 0;
                app_init_buffers(&app);
                app.force_texture_recreate = 1;
                app.dirty_slices = 1;
                if (app.repl_out_count < 200)
                    snprintf(app.repl_output[app.repl_out_count++], 128, "  loaded slot %d", idx);
            }
            app.repl_pending_load[0] = 0;
    app.repl_pending_vol = NULL;
    app.repl_pending_has_data = 0;
        }

        /* process buffered drops: attachments if pending_attach was set by panel */
        if (app.drop_count > 0 && app.num_slots > 0 && app.pending_attach) {
            int type = (app.pending_attach == 1);
            for (int i = 0; i < app.drop_count; i++) {
                add_attachment_to_slot(&app, app.active_slot, app.drop_paths[i], type);
                app.dirty_contrast = 1;
            }
            app.drop_count = 0;
            app.pending_attach = 0;
        }
    }

    vbl_bridge_shutdown();
    CloseWindow();
    return 0;
}
