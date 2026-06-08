#include "app.h"

void save_screenshot(App *app);
void app_init_buffers(App *app);

static int step_size(int dim) { return dim > 0 ? (dim + 99) / 100 : 1; }

static void move_crosshair(App *app, ImageSlot *cs, int nx, int ny, int nz) {
    if (nx < 0) nx = 0; if (nx >= cs->nx) nx = cs->nx - 1;
    if (ny < 0) ny = 0; if (ny >= cs->ny) ny = cs->ny - 1;
    if (nz < 0) nz = 0; if (nz >= cs->nz) nz = cs->nz - 1;

    if (cs->cross_sync) {
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
        cs->cx = nx; cs->cy = ny; cs->cz = nz;
    }
    app->dirty_slices = 1;
}

static void switch_slot(App *app, int new_idx) {
    int old_idx = app->active_slot;
    if (new_idx == old_idx) return;
    ImageSlot *ocs = &app->slots[old_idx];
    (void)ocs;

    app->active_slot = new_idx;
    ImageSlot *ns = &app->slots[new_idx];

    if (ns->cross_sync) {
        app->cx = (int)(app->ch_fx * ns->nx + 0.5);
        app->cy = (int)(app->ch_fy * ns->ny + 0.5);
        app->cz = (int)(app->ch_fz * ns->nz + 0.5);
        if (app->cx >= ns->nx) app->cx = ns->nx - 1;
        if (app->cy >= ns->ny) app->cy = ns->ny - 1;
        if (app->cz >= ns->nz) app->cz = ns->nz - 1;
        if (app->cx < 0) app->cx = 0;
        if (app->cy < 0) app->cy = 0;
        if (app->cz < 0) app->cz = 0;
    }

    app->force_texture_recreate = 1;
    app->dirty_slices = 1;
}

void process_hotkeys(App *app) {
    if (app->num_slots == 0) return;

    /* skip navigation when REPL tab is focused */
    if (app->active_panel_tab == 5) return;

    ImageSlot *cs = &app->slots[app->active_slot];
    if (cs->loading && cs->ct != 0) {
        cs->ct = 0;
        app->dirty_slices = 1;
    }

    int old_cx = app->cx, old_cy = app->cy, old_cz = app->cz;
    int step = 1;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) step = 5;

    /* Arrow keys */
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        int nx = app->cx, ny = app->cy, nz = app->cz;
        switch (app->focus) {
        case 0: case 2: if (nx + step < cs->nx) nx += step; break;
        case 1:          if (ny - step >= 0) ny -= step; break;
        }
        move_crosshair(app, cs, nx, ny, nz);
    }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        int nx = app->cx, ny = app->cy, nz = app->cz;
        switch (app->focus) {
        case 0: case 2: if (nx - step >= 0) nx -= step; break;
        case 1:          if (ny + step < cs->ny) ny += step; break;
        }
        move_crosshair(app, cs, nx, ny, nz);
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        int nx = app->cx, ny = app->cy, nz = app->cz;
        switch (app->focus) {
        case 0:          if (ny - step >= 0) ny -= step; break;
        case 1: case 2:  if (nz - step >= 0) nz -= step; break;
        }
        move_crosshair(app, cs, nx, ny, nz);
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        int nx = app->cx, ny = app->cy, nz = app->cz;
        switch (app->focus) {
        case 0:          if (ny + step < cs->ny) ny += step; break;
        case 1: case 2:  if (nz + step < cs->nz) nz += step; break;
        }
        move_crosshair(app, cs, nx, ny, nz);
    }

    /* Page Up/Down */
    if (IsKeyPressed(KEY_PAGE_UP) || IsKeyPressedRepeat(KEY_PAGE_UP)) {
        int s = step_size(app->focus == 1 ? cs->nx : app->focus == 2 ? cs->ny : cs->nz);
        int nx = app->cx, ny = app->cy, nz = app->cz;
        if (app->focus == 0) nz -= s;
        else if (app->focus == 1) nx -= s;
        else ny -= s;
        move_crosshair(app, cs, nx, ny, nz);
    }
    if (IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) {
        int s = step_size(app->focus == 1 ? cs->nx : app->focus == 2 ? cs->ny : cs->nz);
        int nx = app->cx, ny = app->cy, nz = app->cz;
        if (app->focus == 0) nz += s;
        else if (app->focus == 1) nx += s;
        else ny += s;
        move_crosshair(app, cs, nx, ny, nz);
    }

    /* Tab: cycle focus */
    if (IsKeyPressed(KEY_TAB)) {
        app->focus = (app->focus + 1) % 3;
    }

    /* Backtick: cycle to next slot */
    if (IsKeyPressed(KEY_GRAVE) && app->num_slots > 1) {
        int new_idx = (app->active_slot + 1) % app->num_slots;
        switch_slot(app, new_idx);
        cs = &app->slots[app->active_slot];
        old_cx = app->cx; old_cy = app->cy; old_cz = app->cz;
    }

    /* 1-9: direct slot selection */
    for (int i = 0; i < 9; i++) {
        if (IsKeyPressed(KEY_ONE + i) && i < app->num_slots) {
            switch_slot(app, i);
            cs = &app->slots[app->active_slot];
            old_cx = app->cx; old_cy = app->cy; old_cz = app->cz;
        }
    }

    /* X: toggle crosshair sync for this slot */
    if (IsKeyPressed(KEY_X) && !(IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))) {
        cs->cross_sync = !cs->cross_sync;
        if (cs->cross_sync) {
            cs->cx = app->cx; cs->cy = app->cy; cs->cz = app->cz;
        } else {
            app->cx = cs->cx; app->cy = cs->cy; app->cz = cs->cz;
        }
        app->dirty_slices = 1;
    }

    /* V: toggle overlay */
    if (IsKeyPressed(KEY_V) && cs->num_ovls > 0) {
        cs->ovls[0].enabled = !cs->ovls[0].enabled;
        app->dirty_contrast = 1;
    }

    /* Z: toggle zoom sync for this slot */
    if (IsKeyPressed(KEY_Z) && !(IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))) {
        cs->zoom_sync = !cs->zoom_sync;
        if (cs->zoom_sync)
            cs->zoom = app->zoom;
        else
            app->zoom = cs->zoom;
    }

    /* Super+[ / Super+] : zoom in/out */
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
        if (cs->zoom_sync) {
            if (IsKeyPressed(KEY_LEFT_BRACKET)) {
                double nz = app->zoom / 1.25; if (nz >= 1.0) app->zoom = nz;
            }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                double nz = app->zoom * 1.25; if (nz <= 16.0) app->zoom = nz;
            }
            if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) {
                app->zoom = 1.0;
            }
            for (int i = 0; i < app->num_slots; i++)
                if (app->slots[i].zoom_sync) app->slots[i].zoom = app->zoom;
        } else {
            if (IsKeyPressed(KEY_LEFT_BRACKET)) {
                double nz = cs->zoom / 1.25; if (nz >= 1.0) cs->zoom = nz;
            }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                double nz = cs->zoom * 1.25; if (nz <= 16.0) cs->zoom = nz;
            }
            if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) {
                cs->zoom = 1.0;
            }
        }
    }

    /* [ and ]: 4D time scrubbing (no Super) */
    int nosuper = !IsKeyDown(KEY_LEFT_SUPER) && !IsKeyDown(KEY_RIGHT_SUPER);
    int available_t = cs->loaded_t_count > 0 ? cs->loaded_t_count : (cs->nt > 0 ? cs->nt : 1);
    if (cs->nt > 1 && available_t > 1 && nosuper) {
        if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressedRepeat(KEY_LEFT_BRACKET)) {
            int ts = (available_t + 99) / 100; if (ts < 1) ts = 1;
            cs->ct -= ts; if (cs->ct < 0) cs->ct += available_t;
            app->dirty_slices = 1;
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressedRepeat(KEY_RIGHT_BRACKET)) {
            int ts = (available_t + 99) / 100; if (ts < 1) ts = 1;
            cs->ct += ts; if (cs->ct >= available_t) cs->ct -= available_t;
            app->dirty_slices = 1;
        }
    } else if (cs->nt > 1 && cs->ct != 0) {
        cs->ct = 0;
        app->dirty_slices = 1;
    }

    /* Super+B: toggle sidebar */
    if (IsKeyPressed(KEY_B) && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && app->num_slots > 1) {
        app->sidebar_expanded = !app->sidebar_expanded;
        app->sidebar_w = app->sidebar_expanded ? 240 : 100;
        app->sidebar_scroll_y = 0;
    }

    /* S: screenshot */
    if (IsKeyPressed(KEY_S) && !(IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && !IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT)) {
        save_screenshot(app);
    }

    /* Scroll wheel — check sidebar first, then viewports */
    float mw = GetMouseWheelMove();
    if (mw != 0.0f) {
        int mx_ = GetMouseX(), my_ = GetMouseY();
        if (app->num_slots > 1 && mx_ < app->sidebar_w) {
            int exp = app->sidebar_expanded;
            int thumb_sz2 = exp ? 160 : 64;
            int line_h2 = 14;
            int text_lines2 = exp ? 3 : 1;
            int row_h2 = thumb_sz2 + text_lines2 * (line_h2 + 2) + 8;
            int total_h2 = 8 + app->num_slots * row_h2;
            int max_scroll = total_h2 - app->win_h;
            if (max_scroll < 0) max_scroll = 0;
            app->sidebar_scroll_y -= (int)(mw * 40.0f);
            if (app->sidebar_scroll_y < 0) app->sidebar_scroll_y = 0;
            if (app->sidebar_scroll_y > max_scroll) app->sidebar_scroll_y = max_scroll;
        } else {
            int delta = (mw > 0) ? 1 : -1;
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                int available_t2 = cs->loaded_t_count > 0 ? cs->loaded_t_count : (cs->nt > 0 ? cs->nt : 1);
                if (cs->nt > 1 && available_t2 > 1) cs->ct = (cs->ct + delta + available_t2) % available_t2;
                else if (cs->nt > 1) cs->ct = 0;
            } else {
                int s = step_size(app->focus == 1 ? cs->nx : app->focus == 2 ? cs->ny : cs->nz);
                int nx = app->cx, ny = app->cy, nz = app->cz;
                switch (app->focus) {
                case 0: nz += delta * s; break;
                case 1: nx += delta * s; break;
                case 2: ny += delta * s; break;
                }
                move_crosshair(app, cs, nx, ny, nz);
            }
        }
    }

    /* Q / Escape: quit */
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) {
        app->should_close = 1;
    }

    /* update proportions ONLY if crosshair moved via navigation and synced */
    if ((app->cx != old_cx || app->cy != old_cy || app->cz != old_cz) && cs->cross_sync) {
        app->ch_fx = cs->nx > 1 ? (double)app->cx / cs->nx : 0.5;
        app->ch_fy = cs->ny > 1 ? (double)app->cy / cs->ny : 0.5;
        app->ch_fz = cs->nz > 1 ? (double)app->cz / cs->nz : 0.5;
    }
}
