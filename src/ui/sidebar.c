#include <stdio.h>
#include "app.h"

/* colormaps (mirrors render.c, without duplicating raylib structs) */
typedef void (*cmap_fn)(uint8_t *, uint8_t *, uint8_t *, float);

static void thumb_gray(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    int v = (int)(t * 255.0f);
    if (v < 0) v = 0; if (v > 255) v = 255;
    *r = *g = *b = (uint8_t)v;
}
static void thumb_hot(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    if (t < 0.33f)      { *r = (uint8_t)(t/0.33f*255); *g = 0; *b = 0; }
    else if (t < 0.66f) { *r = 255; *g = (uint8_t)((t-0.33f)/0.33f*255); *b = 0; }
    else                { *r = 255; *g = 255; *b = (uint8_t)((t-0.66f)/0.34f*255); }
}
static void thumb_jet(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    if (t < 0.125f)      { *r = 0; *g = 0; *b = (uint8_t)((t+0.125f)/0.25f*255); }
    else if (t < 0.375f) { *r = 0; *g = (uint8_t)((t-0.125f)/0.25f*255); *b = 255; }
    else if (t < 0.625f) { *r = (uint8_t)((t-0.375f)/0.25f*255); *g = 255; *b = (uint8_t)((0.625f-t)/0.25f*255); }
    else if (t < 0.875f) { *r = 255; *g = (uint8_t)((0.875f-t)/0.25f*255); *b = 0; }
    else                 { *r = (uint8_t)((1.125f-t)/0.25f*255); *g = 0; *b = 0; }
}
static void thumb_bone(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    int v = (int)(t * 255.0f);
    if (v < 0) v = 0; if (v > 255) v = 255;
    *r = *g = *b = (uint8_t)v;
    if (t > 0.15f) *g = (uint8_t)(v * 0.85f);
    if (t > 0.4f)  *b = (uint8_t)(v * 0.7f);
}
static void thumb_coolwarm(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    if (t < 0.5f) {
        float s = t * 2.0f;
        *r = (uint8_t)(s * 59.0f);
        *g = (uint8_t)(s * 76.0f);
        *b = (uint8_t)(s * 192.0f + (1.0f - s) * 64.0f);
    } else {
        float s = (t - 0.5f) * 2.0f;
        *r = (uint8_t)(s * 180.0f + (1.0f - s) * 59.0f);
        *g = (uint8_t)(s * 4.0f + (1.0f - s) * 76.0f);
        *b = (uint8_t)(s * 38.0f + (1.0f - s) * 192.0f);
    }
}
static cmap_fn thumb_cmaps[] = {thumb_gray, thumb_hot, thumb_jet, thumb_bone, thumb_coolwarm};

void generate_thumbnail(App *app, int slot_idx, int z_slice, int timepoint, int out_sz) {
    ImageSlot *slot = &app->slots[slot_idx];
    if (z_slice < 0) z_slice = 0;
    if (z_slice >= slot->nz) z_slice = slot->nz - 1;
    int thumb_sz = out_sz > 0 ? out_sz : 64;
    float *slice = malloc((size_t)slot->nx * slot->ny * sizeof(float));
    uint8_t *rgba = malloc((size_t)thumb_sz * thumb_sz * 4);
    if (!slice || !rgba) {
        /* mark as done so we don't retry infinitely */
        slot->thumb_z = z_slice;
        slot->thumb_ct = timepoint;
        slot->thumb_sz = thumb_sz;
        free(slice); free(rgba); return;
    }

    /* 4D: offset to current timepoint */
    size_t nvox_t0 = (size_t)slot->nx * slot->ny * slot->nz;
    size_t toff = (size_t)timepoint * nvox_t0;
    size_t base = toff + (size_t)z_slice * slot->nx * slot->ny;

    for (int row = 0; row < thumb_sz; row++) {
        int src_row = slot->ny - 1 - (row * slot->ny / thumb_sz);
        for (int col = 0; col < thumb_sz; col++) {
            int src_col = col * slot->nx / thumb_sz;
            float v = slot->vol[base + (size_t)src_row * slot->nx + src_col];
            double t = (v - slot->vmin) / (slot->vmax - slot->vmin + 1e-10);
            if (t < 0) t = 0; if (t > 1) t = 1;
            uint8_t *p = rgba + ((size_t)row * thumb_sz + col) * 4;
            int ci = slot->cmap >= 0 && slot->cmap < 5 ? slot->cmap : 0;
            thumb_cmaps[ci](&p[0], &p[1], &p[2], (float)t);
            p[3] = 255;
        }
    }
    Image img = { .data = rgba, .width = thumb_sz, .height = thumb_sz,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (slot->thumbnail.id != 0) UnloadTexture(slot->thumbnail);
    slot->thumbnail = LoadTextureFromImage(img);
    slot->thumb_z = z_slice;
    slot->thumb_ct = timepoint;
    slot->thumb_sz = thumb_sz;
    slot->thumb_vmin = slot->vmin;
    slot->thumb_vmax = slot->vmax;
    slot->thumb_cmap = slot->cmap;
    free(slice); free(rgba);
}

void draw_sidebar(App *app, Rectangle bounds) {
    if (app->num_slots <= 1) return;

    int expanded = app->sidebar_expanded;
    int thumb_sz = expanded ? 160 : 64;
    int line_h = 14;
    int text_lines = expanded ? 3 : 1; /* filename + dims + voxel */
    int row_h = thumb_sz + text_lines * (line_h + 2) + 8;
    int cx = (int)(bounds.x + bounds.width / 2);
    int scroll = app->sidebar_scroll_y;
    int y = (int)bounds.y + 8 - scroll;

    /* background */
    DrawRectangleRec(bounds, (Color){30,30,35,255});

    /* divider — wider with arrow indicators */
    int div_x = (int)(bounds.x + bounds.width - 10);
    DrawRectangle(div_x, (int)bounds.y, 10, (int)bounds.height, (Color){45,45,50,255});
    DrawRectangle(div_x, (int)bounds.y, 1, (int)bounds.height, (Color){55,55,60,255});
    /* arrow indicator at top of divider */
    const char *arrow = expanded ? "\xC2\xAB" : "\xC2\xBB"; /* « or » */
    DrawText(arrow, div_x + 1, (int)bounds.y + 8, 10, (Color){100,100,110,255});

    int dragging = (app->sidebar_state == 2);
    int drag_slot = app->sidebar_drag_slot;

    for (int i = 0; i < app->num_slots; i++) {
        if (dragging && i == drag_slot) continue;

        int ty = y;
        if (ty + row_h < (int)bounds.y) { y += row_h; continue; }
        if (ty > (int)(bounds.y + bounds.height)) break;

        ImageSlot *slot = &app->slots[i];

        /* thumbnail Z: synced → follow global via proportions; unsynced inactive → center */
        int tn_z, tn_ct;
        if (i == app->active_slot) {
            tn_z  = slot->cross_sync ? app->cz : slot->cz;
            tn_ct = slot->ct;
        } else if (slot->cross_sync) {
            /* inactive but synced: map global crosshair via proportions */
            tn_z  = (int)(app->ch_fz * slot->nz + 0.5);
            tn_ct = slot->ct;
        } else {
            /* inactive, unsynced: stable center preview */
            tn_z  = slot->nz / 2;
            tn_ct = slot->ct;
        }
        if (tn_z < 0) tn_z = 0;
        if (tn_z >= slot->nz) tn_z = slot->nz - 1;

        /* regenerate if Z, timepoint, size, contrast, or cmap changed */
        if (slot->thumbnail.id == 0 ||
            slot->thumb_z != tn_z || slot->thumb_ct != tn_ct ||
            slot->thumb_sz != thumb_sz ||
            slot->thumb_vmin != slot->vmin || slot->thumb_vmax != slot->vmax ||
            slot->thumb_cmap != slot->cmap) {
            generate_thumbnail(app, i, tn_z, tn_ct, thumb_sz);
        }

        int tx = cx - thumb_sz / 2;
        int label_y = ty + thumb_sz + 4;

        /* thumbnail bg */
        DrawRectangle(tx, ty, thumb_sz, thumb_sz, (Color){20,20,20,255});
        if (slot->thumbnail.id > 0) {
            int tsz = slot->thumb_sz > 0 ? slot->thumb_sz : 64;
            DrawTexturePro(slot->thumbnail, (Rectangle){0,0,tsz,tsz},
                           (Rectangle){tx, ty, thumb_sz, thumb_sz},
                           (Vector2){0,0}, 0, WHITE);
        }

        /* active border */
        if (i == app->active_slot) {
            DrawRectangleLinesEx((Rectangle){tx, ty, thumb_sz, thumb_sz}, 2,
                                 (Color){0,200,255,255});
        }

        /* right-click to open context menu */
        if (app->num_slots > 1) {
            Vector2 smp = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) &&
                CheckCollisionPointRec(smp, (Rectangle){tx, ty, thumb_sz, row_h})) {
                app->sctx_visible = 1;
                app->sctx_x = (int)smp.x;
                app->sctx_y = (int)smp.y;
                app->sctx_slot = i;
                app->sctx_block_click = 1;
                app->sidebar_state = 0;
            }
        }

        /* filename — always show */
        char buf[256];
        int max_chars = (int)(bounds.width - 16) / 7;
        if (max_chars < 4) max_chars = 4;
        int fnlen = (int)strlen(slot->filename);
        if (fnlen > max_chars)
            snprintf(buf, sizeof(buf), "%.*s...", max_chars - 3, slot->filename);
        else
            snprintf(buf, sizeof(buf), "%s", slot->filename);
        DrawText(buf, tx, label_y, 10,
                 i == app->active_slot ? (Color){220,220,220,255} : (Color){160,160,160,255});
        label_y += line_h + 2;

        if (expanded) {
            /* dims */
            snprintf(buf, sizeof(buf), "%dx%dx%d", slot->nx, slot->ny, slot->nz);
            DrawText(buf, tx, label_y, 10, (Color){130,130,130,255});
            label_y += line_h + 2;

            /* voxel size */
            snprintf(buf, sizeof(buf), "%.1fx%.1fx%.1f", slot->dx, slot->dy, slot->dz);
            DrawText(buf, tx, label_y, 10, (Color){100,100,100,255});
        }

        y += row_h;
    }

    if (dragging) {
        int dst = app->sidebar_drag_dst;
        int ins_y = (int)bounds.y + 8 + dst * row_h - scroll - 2;
        /* only draw insertion line if visible */
        if (ins_y >= (int)bounds.y - 4 && ins_y <= (int)(bounds.y + bounds.height)) {
            DrawRectangle((int)bounds.x + 4, ins_y, (int)bounds.width - 10, 4, (Color){0,200,255,255});
        }
        ImageSlot *gs = &app->slots[drag_slot];
        if (gs->thumbnail.id > 0) {
            int ghost_x = app->sidebar_drag_mx - thumb_sz / 2;
            int ghost_y = app->sidebar_drag_my - thumb_sz / 2;
            /* clamp ghost to avoid extreme off-screen coords crashing GPU */
            if (ghost_x < -thumb_sz) ghost_x = -thumb_sz;
            if (ghost_y < -thumb_sz) ghost_y = -thumb_sz;
            if (ghost_x > app->win_w) ghost_x = app->win_w;
            if (ghost_y > app->win_h) ghost_y = app->win_h;
            int tsz = gs->thumb_sz > 0 ? gs->thumb_sz : 64;
            DrawTexturePro(gs->thumbnail, (Rectangle){0,0,tsz,tsz},
                           (Rectangle){ghost_x, ghost_y, thumb_sz, thumb_sz},
                           (Vector2){0,0}, 0, (Color){255,255,255,160});
        }
    }

    /* ── sidebar context menu ── */
    if (app->sctx_visible) {
        int mx = app->sctx_x, my = app->sctx_y;
        int mw = 90, mh = 20;
        Rectangle mr = {mx, my, mw, mh};
        Vector2 cm = GetMousePosition();
        int hover = CheckCollisionPointRec(cm, mr);
        DrawRectangleRec(mr, hover ? (Color){80,40,40,255} : (Color){50,50,55,255});
        DrawRectangleLinesEx(mr, 1, (Color){100,70,70,255});
        DrawText("Delete", mx + 8, my + 4, 12, hover ? (Color){255,100,100,255} : (Color){220,80,80,255});

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (hover) {
                app->pending_remove_slot = app->sctx_slot;
                app->sctx_visible = 0;
            } else {
                app->sctx_visible = 0;
            }
        }
    }
}
