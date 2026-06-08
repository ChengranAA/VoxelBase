#include <stdio.h>
#include "raygui.h"
#include "app.h"
#include "plugins/vbl/vbl_bridge.h"

int add_slot_from_file(App *app, const char *path);
void app_init_buffers(App *app);

static const char *tab_names[] = {"File", "Cursor", "Contrast", "View", "Layers", "REPL"};

static const char *cmap_names = "Gray;Hot;Jet;Bone;Coolwarm";

/* ── VBL keywords for highlighting and completion ────────────── */
static const char *vbl_keywords[] = {
    "def", "print", "show", "slot", "save", "load", "exit", "help",
    "+", "-", "*", "/", "bc+", "bc-", "bc*", "bc/",
    "mean", "tmean", "stdev", "min", "max", "sum", "tstd",
    "add", "sub", "mul", "div",
    "smooth", "crop", "pad", "threshold", "mask", "mask-int",
    "correlate", "seed", "noise", "vol3d", "vol4d",
    "bandpass", "detrend",
    "slice", "tslice", "ts-range", "copy", "voxel",
    "eq", "gt", "lt", "and", "or",
    "affine", "translate", "rotate", "scale", "apply",
    "plot-line", "plot-hist", "add-line", "clear",
    "load-script", "repl-reset",
    "vol3d", "vol4d", "timeseries", "corrmap", "mask", "scalar", "affine",
    NULL
};

static int is_vbl_keyword(const char *word, int len) {
    for (int i = 0; vbl_keywords[i]; i++) {
        if ((int)strlen(vbl_keywords[i]) == len &&
            strncmp(vbl_keywords[i], word, (size_t)len) == 0)
            return 1;
    }
    return 0;
}

/* ── syntax-highlighted REPL input rendering ────────────────── */
static void draw_repl_input_hl(const char *text, int cursor, const char *hint,
                                int x, int y, int font_size) {
    int len = (int)strlen(text);
    int px = x;
    Color c;

    for (int i = 0; i <= len; i++) {
        char ch = (i < len) ? text[i] : '_';
        int is_cursor = (i == cursor);

        if (is_cursor) {
            /* draw cursor block */
            DrawRectangle(px, y, 8, font_size + 2, (Color){100, 220, 100, 180});
            if (i >= len) {
                /* cursor at end — draw hint as ghost text after cursor */
                if (hint && hint[0]) {
                    int hint_x = px + 10;
                    DrawText(hint, hint_x, y, font_size, (Color){90, 90, 100, 180});
                }
                px += 8;
                break;
            }
            ch = text[i];
        }

        /* determine color by context */
        if (ch == '(' || ch == ')') {
            c = (Color){140, 140, 155, 255};
        } else if (ch == '"') {
            c = (Color){255, 180, 80, 255};
        } else if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
            int j = i;
            while (j > 0 && text[j-1] != ' ' && text[j-1] != '(' && text[j-1] != ')') j--;
            int is_num = 0;
            for (int k = j; k < len && text[k] != ' ' && text[k] != '(' && text[k] != ')'; k++) {
                if ((text[k] >= '0' && text[k] <= '9') || text[k] == '.' ||
                    (text[k] == '-' && k == j)) is_num = 1;
                else { is_num = 0; break; }
            }
            c = is_num ? (Color){255, 220, 100, 255} : (Color){200, 200, 200, 255};
        } else if (ch == ';') {
            c = (Color){100, 160, 100, 255};
        } else if (ch == ' ' || ch == '\t') {
            c = (Color){200, 200, 200, 255};
        } else {
            int wstart = i;
            while (wstart > 0 && text[wstart-1] != ' ' && text[wstart-1] != '(' &&
                   text[wstart-1] != ')' && text[wstart-1] != '"') wstart--;
            int wlen = 0;
            while (wstart + wlen < len && text[wstart + wlen] != ' ' &&
                   text[wstart + wlen] != '(' && text[wstart + wlen] != ')' &&
                   text[wstart + wlen] != '"') wlen++;
            c = is_vbl_keyword(text + wstart, wlen)
                ? (Color){100, 220, 180, 255}
                : (Color){200, 200, 200, 255};
        }

        char buf[2] = {ch, 0};
        DrawText(buf, px, y, font_size, c);
        px += MeasureText(buf, font_size) + 2;
    }
}

/* ── tab completion ─────────────────────────────────────────── */
/* Compute ghost hint for the word at cursor. Tab accepts it. */
static void repl_update_hint(App *app) {
    app->repl_hint[0] = '\0';
    int len = (int)strlen(app->repl_input);
    if (len == 0) return;

    /* find word start before cursor */
    int wstart = app->repl_cursor;
    while (wstart > 0 && app->repl_input[wstart - 1] != ' ' &&
           app->repl_input[wstart - 1] != '(' && app->repl_input[wstart - 1] != ')')
        wstart--;
    int wlen = app->repl_cursor - wstart;
    if (wlen == 0) return;

    /* find first match that extends the current word */
    for (int i = 0; vbl_keywords[i]; i++) {
        if (strncmp(vbl_keywords[i], app->repl_input + wstart, (size_t)wlen) == 0 &&
            (int)strlen(vbl_keywords[i]) > wlen) {
            /* store the remainder as hint */
            snprintf(app->repl_hint, sizeof(app->repl_hint),
                     "%s", vbl_keywords[i] + wlen);
            return;
        }
    }
}

/* Accept the ghost hint (Tab key) */
static void repl_accept_hint(App *app) {
    if (app->repl_hint[0] == '\0') return;
    int len = (int)strlen(app->repl_input);
    int hlen = (int)strlen(app->repl_hint);
    if (len + hlen >= 250) return;
    memmove(app->repl_input + app->repl_cursor + hlen,
            app->repl_input + app->repl_cursor,
            len - app->repl_cursor + 1);
    memcpy(app->repl_input + app->repl_cursor, app->repl_hint, (size_t)hlen);
    app->repl_cursor += hlen;
    app->repl_hint[0] = '\0';
}

/* ── syntax-highlighted output line ─────────────────────────── */
static void draw_output_hl(const char *text, int x, int y, int font_size) {
    int len = (int)strlen(text);
    int px = x;
    for (int i = 0; i < len; i++) {
        char ch = text[i];
        Color c = (Color){180, 180, 190, 255};
        if (ch == '(' || ch == ')') {
            c = (Color){140, 140, 155, 255};
        } else if (ch == '"') {
            c = (Color){255, 180, 80, 255};
        } else if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
            int j = i;
            while (j > 0 && text[j-1] != ' ' && text[j-1] != '(' && text[j-1] != ')') j--;
            int is_num = 1;
            for (int k = j; k < len && text[k] != ' ' && text[k] != '(' && text[k] != ')'; k++) {
                if (!((text[k] >= '0' && text[k] <= '9') || text[k] == '.' ||
                      (text[k] == '-' && k == j))) { is_num = 0; break; }
            }
            if (is_num) c = (Color){255, 220, 100, 255};
        } else if (ch == ';') {
            c = (Color){100, 160, 100, 255};
        } else {
            int wstart = i;
            while (wstart > 0 && text[wstart-1] != ' ' && text[wstart-1] != '(' &&
                   text[wstart-1] != ')' && text[wstart-1] != '"') wstart--;
            int wlen = 0;
            while (wstart + wlen < len && text[wstart + wlen] != ' ' &&
                   text[wstart + wlen] != '(' && text[wstart + wlen] != ')' &&
                   text[wstart + wlen] != '"') wlen++;
            if (is_vbl_keyword(text + wstart, wlen))
                c = (Color){100, 220, 180, 255};
        }
        char buf[2] = {ch, 0};
        DrawText(buf, px, y, font_size, c);
        px += MeasureText(buf, font_size) + 2;
    }
}

/* ── REPL panel (shared between zero-slot and normal mode) ──── */
static void draw_repl_panel(App *app, Rectangle bounds) {
    const int input_font = 14;
    const int output_font = 14;
    const int output_lh  = 22;  /* line height for output */
    const int bar_h = 28;       /* input bar height */

    Rectangle inner = {bounds.x + 6, bounds.y + 6,
                       bounds.width - 12, bounds.height - 12};

    /* header: show "BareMRI REPL" when no slots */
    if (app->num_slots == 0) {
        DrawText("VoxelBase REPL  (drop .nii to load)", (int)inner.x, (int)inner.y, 14,
                 (Color){100, 220, 180, 255});
        inner.y += 20;
        inner.height -= 20;
    }

    int out_area_h = (int)inner.height - bar_h - 8;
    if (out_area_h < 40) out_area_h = 40;

    /* output area */
    DrawRectangleRec((Rectangle){inner.x, inner.y, inner.width, out_area_h},
                     (Color){18, 18, 22, 255});
    DrawRectangleLines((int)inner.x, (int)inner.y, (int)inner.width, out_area_h,
                        (Color){50, 50, 55, 255});

    int out_w = (int)inner.width - 8;
    int max_text_w = out_w - 4;
    /* fixed char width at this font size (~8px avg for raylib default) */
    int char_w = 8;
    int max_chars = max_text_w / char_w;
    if (max_chars < 20) max_chars = 20;
    Vector2 mp = GetMousePosition();

    if (app->repl_mode == 0) {
        /* ── text mode with wrapping ────────────────────────── */
        int wrapped_lines[1024], wrapped_offs[1024];
        int wrapped_count = 0;
        for (int li = 0; li < app->repl_out_count && wrapped_count < 1023; li++) {
            const char *line = app->repl_output[li];
            int line_len = (int)strlen(line);
            int offset = 0;
            do {
                wrapped_lines[wrapped_count] = li;
                wrapped_offs[wrapped_count] = offset;
                wrapped_count++;
                int fit = line_len - offset;
                if (fit > max_chars) fit = max_chars;
                offset += fit;
            } while (offset < line_len);
        }

        /* mouse wheel scroll */
        int max_vis = out_area_h / output_lh;
        int max_scroll = wrapped_count - max_vis;
        if (max_scroll < 0) max_scroll = 0;
        if (CheckCollisionPointRec(mp, (Rectangle){inner.x, inner.y, inner.width, (float)out_area_h})) {
            static float wheel_acc = 0;
            wheel_acc += GetMouseWheelMove();
            int steps = (int)wheel_acc;
            if (steps != 0) {
                wheel_acc -= (float)steps;
                app->repl_scroll += steps;
                if (app->repl_scroll < 0) app->repl_scroll = 0;
                if (app->repl_scroll > max_scroll) app->repl_scroll = max_scroll;
            }
        }
        /* auto-scroll to bottom when new output appears */
        static int last_out_count = 0;
        if (app->repl_out_count != last_out_count) {
            app->repl_scroll = 0;
            last_out_count = app->repl_out_count;
        }

        int start = wrapped_count - max_vis - app->repl_scroll;
        if (start < 0) start = 0;
        int oy = (int)inner.y + 4;
        for (int wi = start; wi < wrapped_count && oy < (int)inner.y + out_area_h - output_lh; wi++) {
            const char *line = app->repl_output[wrapped_lines[wi]] + wrapped_offs[wi];
            int len = (int)strlen(line);
            int fit = len;
            if (fit > max_chars) fit = max_chars;
            char tmp[128];
            int n = fit < 127 ? fit : 127;
            memcpy(tmp, line, (size_t)n);
            tmp[n] = '\0';
            draw_output_hl(tmp, (int)inner.x + 4, oy, output_font);
            oy += output_lh;
        }

        /* scrollbar */
        if (max_scroll > 0) {
            int sb_x = (int)inner.x + out_w - 6;
            int sb_h = out_area_h * max_vis / wrapped_count;
            if (sb_h < 8) sb_h = 8;
            int sb_y = (int)inner.y + (out_area_h - sb_h) * app->repl_scroll / max_scroll;
            DrawRectangle(sb_x, sb_y, 4, sb_h, (Color){80, 80, 90, 200});
        }

        DrawText("Ctrl+T canvas", (int)inner.x + out_w - 80, (int)inner.y + out_area_h - 14, 10,
                 (Color){70, 70, 80, 255});
    } else {
        /* ── canvas mode ─────────────────────────────────────── */
        int cx = (int)inner.x + 4, cy = (int)inner.y + 4;
        int cw = out_w, ch = out_area_h - 16;
        int margin = 30;
        int px0 = cx + margin, py0 = cy + ch - margin;
        int pw = cw - margin - 8, ph = ch - margin - 4;

        /* axes */
        DrawLine(px0, cy + 4, px0, py0, (Color){100, 100, 110, 255});
        DrawLine(px0, py0, px0 + pw, py0, (Color){100, 100, 110, 255});

        /* axis labels */
        if (app->plot_xlabel[0])
            DrawText(app->plot_xlabel, px0 + pw/2 - 20, py0 + 8, 10, (Color){120, 120, 130, 255});
        if (app->plot_ylabel[0])
            DrawText(app->plot_ylabel, cx + 2, cy + 8, 10, (Color){120, 120, 130, 255});

        if (app->plot_hist_count > 0 && app->plot_hist_count <= 64) {
            /* histogram */
            int nb = app->plot_hist_count;
            int bar_w = pw / nb;
            if (bar_w < 2) bar_w = 2;
            int max_h = 0;
            for (int i = 0; i < nb; i++)
                if (app->plot_hist[i] > max_h) max_h = app->plot_hist[i];
            if (max_h < 1) max_h = 1;
            for (int i = 0; i < nb; i++) {
                int bh = (int)((float)app->plot_hist[i] / (float)max_h * (float)ph);
                if (bh < 1 && app->plot_hist[i] > 0) bh = 1;
                int bx = px0 + i * bar_w;
                DrawRectangle(bx, py0 - bh, bar_w - 1, bh, (Color){100, 180, 220, 220});
                if (nb <= 16 && bar_w > 20) {
                    char blbl[8];
                    float bv = app->plot_hist_min + (float)i / (float)(nb - 1) *
                               (app->plot_hist_max - app->plot_hist_min);
                    snprintf(blbl, sizeof(blbl), "%.1f", bv);
                    DrawText(blbl, bx, py0 + 2, 8, (Color){120, 120, 130, 255});
                }
            }
        } else if (app->plot_series_count > 0) {
            /* multi-series line/scatter with legend */
            float xmin, xmax, ymin, ymax;
            int has_data = 0;
            for (int s = 0; s < app->plot_series_count; s++) {
                for (int i = 0; i < app->plot_series[s].count; i++) {
                    float vx = app->plot_series[s].xs[i];
                    float vy = app->plot_series[s].ys[i];
                    if (!has_data) {
                        xmin = xmax = vx; ymin = ymax = vy; has_data = 1;
                    } else {
                        if (vx < xmin) xmin = vx; if (vx > xmax) xmax = vx;
                        if (vy < ymin) ymin = vy; if (vy > ymax) ymax = vy;
                    }
                }
            }
            if (!has_data) { xmin = 0; xmax = 1; ymin = 0; ymax = 1; }
            float xr = xmax - xmin; if (xr < 0.0001f) xr = 1.0f;
            float yr = ymax - ymin; if (yr < 0.0001f) yr = 1.0f;

            for (int s = 0; s < app->plot_series_count; s++) {
                Color lc = app->plot_series[s].color;
                for (int i = 0; i < app->plot_series[s].count - 1; i++) {
                    int sx0 = px0 + (int)((app->plot_series[s].xs[i] - xmin) / xr * (float)pw);
                    int sy0 = py0 - (int)((app->plot_series[s].ys[i] - ymin) / yr * (float)ph);
                    int sx1 = px0 + (int)((app->plot_series[s].xs[i+1] - xmin) / xr * (float)pw);
                    int sy1 = py0 - (int)((app->plot_series[s].ys[i+1] - ymin) / yr * (float)ph);
                    DrawLine(sx0, sy0, sx1, sy1, lc);
                }
                for (int i = 0; i < app->plot_series[s].count; i++) {
                    int sx = px0 + (int)((app->plot_series[s].xs[i] - xmin) / xr * (float)pw);
                    int sy = py0 - (int)((app->plot_series[s].ys[i] - ymin) / yr * (float)ph);
                    DrawCircle(sx, sy, 2, lc);
                }
            }
            /* legend — top-left of plot area */
            int lx = px0 + 4, ly = cy + 4;
            for (int s = 0; s < app->plot_series_count && ly < py0 - 10; s++) {
                DrawRectangle(lx, ly, 10, 10, app->plot_series[s].color);
                DrawText(app->plot_series[s].name, lx + 14, ly, 10, (Color){220,220,220,255});
                ly += 14;
            }
        } else {
            DrawText("No plot data. Try (plot-line xs ys) or (plot-hist data bins)",
                     cx + 8, cy + 8, 12, (Color){90, 90, 100, 255});
        }
        /* toggle hint */
        DrawText("Ctrl+T text", (int)inner.x + out_w - 70, (int)inner.y + out_area_h - 14, 10,
                 (Color){70, 70, 80, 255});
    }

    /* input bar at bottom */
    int inp_y = (int)inner.y + out_area_h + 4;
    DrawRectangleRec((Rectangle){inner.x, inp_y, inner.width, bar_h}, (Color){28, 28, 33, 255});
    DrawRectangleLines((int)inner.x, inp_y, (int)inner.width, bar_h, (Color){60, 120, 60, 255});
    DrawText(">", (int)inner.x + 4, inp_y + 5, input_font, (Color){100, 220, 100, 255});

    /* syntax-highlighted input with cursor + ghost hint */
    draw_repl_input_hl(app->repl_input, app->repl_cursor, app->repl_hint,
                       (int)inner.x + 22, inp_y + 5, input_font);

    /* ── keyboard input ──────────────────────────────────────── */
    int len = (int)strlen(app->repl_input);

    /* cursor movement */
    if (IsKeyPressed(KEY_LEFT) && app->repl_cursor > 0) {
        app->repl_cursor--;
        repl_update_hint(app);
    }
    if (IsKeyPressed(KEY_RIGHT) && app->repl_cursor < len) {
        app->repl_cursor++;
        repl_update_hint(app);
    }
    if (IsKeyPressed(KEY_HOME)) { app->repl_cursor = 0; repl_update_hint(app); }
    if (IsKeyPressed(KEY_END))  { app->repl_cursor = len; repl_update_hint(app); }

    /* Ctrl+A: beginning of line  /  Ctrl+E: end of line */
    if (IsKeyPressed(KEY_A) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
        { app->repl_cursor = 0; repl_update_hint(app); }
    if (IsKeyPressed(KEY_E) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
        { app->repl_cursor = len; repl_update_hint(app); }

    /* Ctrl+T: toggle canvas/text mode */
    if (IsKeyPressed(KEY_T) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
        app->repl_mode = !app->repl_mode;

    /* clipboard paste (Cmd+V / Ctrl+V) */
    if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) ||
                                 IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        const char *clip = GetClipboardText();
        if (clip) {
            int clen = (int)strlen(clip);
            /* trim trailing newline */
            while (clen > 0 && (clip[clen-1] == '\n' || clip[clen-1] == '\r')) clen--;
            if (len + clen < 250) {
                memmove(app->repl_input + app->repl_cursor + clen,
                        app->repl_input + app->repl_cursor,
                        len - app->repl_cursor + 1);
                memcpy(app->repl_input + app->repl_cursor, clip, (size_t)clen);
                app->repl_cursor += clen;
                repl_update_hint(app);
            }
        }
    }

    /* tab completion */
    if (IsKeyPressed(KEY_TAB)) {
        repl_accept_hint(app);
        repl_update_hint(app);
    }

    int old_len = len;
    int key = GetCharPressed();
    while (key > 0) {
        if (len < 250) {
            memmove(app->repl_input + app->repl_cursor + 1,
                    app->repl_input + app->repl_cursor, len - app->repl_cursor + 1);
            app->repl_input[app->repl_cursor] = (char)key;
            app->repl_cursor++;
        }
        key = GetCharPressed();
        len = (int)strlen(app->repl_input);
    }
    if (len != old_len) repl_update_hint(app);
    if (IsKeyPressed(KEY_BACKSPACE) && app->repl_cursor > 0) {
        memmove(app->repl_input + app->repl_cursor - 1,
                app->repl_input + app->repl_cursor, len - app->repl_cursor + 1);
        app->repl_cursor--;
        repl_update_hint(app);
    }
    if (IsKeyPressed(KEY_DELETE) && app->repl_cursor < len) {
        memmove(app->repl_input + app->repl_cursor,
                app->repl_input + app->repl_cursor + 1, len - app->repl_cursor);
        repl_update_hint(app);
    }

    /* Ctrl+W: delete word backward */
    if (IsKeyPressed(KEY_W) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        int wend = app->repl_cursor;
        while (wend > 0 && app->repl_input[wend - 1] == ' ') wend--;
        while (wend > 0 && app->repl_input[wend - 1] != ' ') wend--;
        int n = app->repl_cursor - wend;
        if (n > 0) {
            memmove(app->repl_input + wend, app->repl_input + app->repl_cursor,
                    len - app->repl_cursor + 1);
            app->repl_cursor = wend;
            repl_update_hint(app);
        }
    }

    /* Ctrl+U: delete to beginning of line */
    if (IsKeyPressed(KEY_U) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (app->repl_cursor > 0) {
            memmove(app->repl_input, app->repl_input + app->repl_cursor,
                    len - app->repl_cursor + 1);
            app->repl_cursor = 0;
            repl_update_hint(app);
        }
    }

    if (IsKeyPressed(KEY_ENTER) && strlen(app->repl_input) > 0) {
        /* exec command */
        if (app->repl_out_count < 200) {
            snprintf(app->repl_output[app->repl_out_count], 128, "> %s", app->repl_input);
            app->repl_out_count++;
        }

        /* VBL → delegate to bridge */
        if (app->repl_input[0] == '(' || app->repl_input[0] == ';' ||
            app->repl_input[0] == '"') {
            vbl_bridge_eval(app, app->repl_input);
        } else {
            if (app->repl_out_count < 200)
                snprintf(app->repl_output[app->repl_out_count++], 128,
                         "  Type (help) for VBL commands");
        }
        app->repl_input[0] = 0;
        app->repl_cursor = 0;
        app->repl_hint[0] = '\0';
    }
}

void draw_panel(App *app, Rectangle bounds) {
    /* Zero-slot mode: REPL-only, no tabs */
    if (app->num_slots == 0) {
        app->active_panel_tab = 5; /* force REPL */
        draw_repl_panel(app, bounds);
        return;
    }
    ImageSlot *cs = &app->slots[app->active_slot];

    /* Tab bar — dynamic width (shrinks with panel) */
    Rectangle tab_rect = {bounds.x, bounds.y, bounds.width, 22};
    float tab_w = tab_rect.width / 6.0f;
    for (int ti = 0; ti < 6; ti++) {
        Rectangle tr = {tab_rect.x + ti * tab_w, tab_rect.y, tab_w, tab_rect.height};
        bool t_active = (ti == app->active_panel_tab) ? true : false;
        GuiToggle(tr, tab_names[ti], &t_active);
        if (t_active && ti != app->active_panel_tab) app->active_panel_tab = ti;
    }
    /* line under tabs */
    DrawRectangle((int)tab_rect.x, (int)(tab_rect.y + tab_rect.height - 1),
                  (int)tab_rect.width, 1, GetColor(GuiGetStyle(TOGGLE, BORDER_COLOR_NORMAL)));

    /* Body — plain raylib draw to avoid raygui scissor interference */
    Rectangle body = {bounds.x, bounds.y + 22, bounds.width, bounds.height - 22};
    DrawRectangleRec(body, (Color){25,25,30,255});
    DrawRectangleLinesEx(body, 1, (Color){50,50,55,255});
    Rectangle inner = {body.x + 6, body.y + 6, body.width - 12, body.height - 12};

    char buf[256];
    int y = (int)inner.y;
    int lh = 18;

    switch (app->active_panel_tab) {

    /* ── File ── */
    case 0: {
        snprintf(buf, sizeof(buf), "File: %s", cs->filename);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        snprintf(buf, sizeof(buf), "Dims: %d x %d x %d x %d", cs->nx, cs->ny, cs->nz, cs->nt);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        snprintf(buf, sizeof(buf), "Voxel: %.2f x %.2f x %.2f mm", cs->dx, cs->dy, cs->dz);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        if (cs->nt > 1) {
            snprintf(buf, sizeof(buf), "TR: %.3f s   Time: %d / %d", cs->tr, cs->ct + 1, cs->nt);
            GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;
        }

        /* intensity range */
        snprintf(buf, sizeof(buf), "Range: %.2f .. %.2f", cs->auto_vmin, cs->auto_vmax);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        /* datatype */
        if (cs->nim) {
            const char *dt = "?";
            switch (cs->nim->datatype) {
            case 2: dt = "uint8"; break;
            case 4: dt = "int16"; break;
            case 8: dt = "int32"; break;
            case 16: dt = "float32"; break;
            case 64: dt = "float64"; break;
            case 256: dt = "int8"; break;
            case 512: dt = "uint16"; break;
            case 768: dt = "uint32"; break;
            }
            snprintf(buf, sizeof(buf), "Type: %s", dt);
            GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;
        }

        /* Remove slot button */
        if (GuiButton((Rectangle){inner.x, y, 80, 20}, "Remove")) {
            app->pending_remove_slot = app->active_slot;
            app->dirty_slices = 1;
        }
        break;
    }

    /* ── Cursor ── */
    case 1: {
        int eff_cx = cs->cross_sync ? app->cx : cs->cx;
        int eff_cy = cs->cross_sync ? app->cy : cs->cy;
        int eff_cz = cs->cross_sync ? app->cz : cs->cz;

        snprintf(buf, sizeof(buf), "X: %d / %d", eff_cx + 1, cs->nx);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        snprintf(buf, sizeof(buf), "Y: %d / %d", cs->ny - eff_cy, cs->ny);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        snprintf(buf, sizeof(buf), "Z: %d / %d", eff_cz + 1, cs->nz);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 2;

        snprintf(buf, sizeof(buf), "Focus: %s",
                 app->focus == 0 ? "Axial" : app->focus == 1 ? "Sagittal" : "Coronal");
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 4;

        /* Cross Sync checkbox */
        bool cross_on = cs->cross_sync != 0;
        GuiCheckBox((Rectangle){inner.x, y, 16, 16}, "Cross Sync", &cross_on);
        if (cross_on != (cs->cross_sync != 0)) {
            cs->cross_sync = cross_on ? 1 : 0;
            if (cs->cross_sync) {
                cs->cx = app->cx; cs->cy = app->cy; cs->cz = app->cz;
            } else {
                app->cx = cs->cx; app->cy = cs->cy; app->cz = cs->cz;
            }
            app->dirty_slices = 1;
        }
        y += 22;

        /* Voxel value at crosshair */
        size_t idx = (size_t)cs->ct * cs->nx * cs->ny * cs->nz
                     + (size_t)eff_cx
                     + (size_t)eff_cy * cs->nx
                     + (size_t)eff_cz * cs->nx * cs->ny;
        if (idx < (size_t)cs->nx * cs->ny * cs->nz * (cs->nt > 0 ? cs->nt : 1)) {
            snprintf(buf, sizeof(buf), "Value: %.4f", cs->vol[idx]);
            GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf); y += lh + 4;
        }

        /* overlay / segmentation values at crosshair */
        for (int oi = 0; oi < cs->num_ovls; oi++) {
            Attachment *ovl = &cs->ovls[oi];
            if (!ovl->enabled || !ovl->vol) continue;
            int oz = eff_cz * ovl->nz / cs->nz;
            int oy = eff_cy * ovl->ny / cs->ny;
            int ox = eff_cx * ovl->nx / cs->nx;
            if (oz >= 0 && oz < ovl->nz && oy >= 0 && oy < ovl->ny && ox >= 0 && ox < ovl->nx) {
                float v = ovl->vol[(size_t)oz * ovl->nx * ovl->ny + (size_t)oy * ovl->nx + ox];
                snprintf(buf, sizeof(buf), "Ovl%d: %.4f", oi+1, (double)v);
                DrawText(buf, (int)inner.x, y, 10, (Color){140,170,220,255}); y += 13;
            }
        }
        for (int si = 0; si < cs->num_segs; si++) {
            Attachment *seg = &cs->segs[si];
            if (!seg->enabled || !seg->vol) continue;
            int sz = eff_cz * seg->nz / cs->nz;
            int sy = eff_cy * seg->ny / cs->ny;
            int sx = eff_cx * seg->nx / cs->nx;
            if (sz >= 0 && sz < seg->nz && sy >= 0 && sy < seg->ny && sx >= 0 && sx < seg->nx) {
                float v = seg->vol[(size_t)sz * seg->nx * seg->ny + (size_t)sy * seg->nx + sx];
                snprintf(buf, sizeof(buf), "Seg%d: %d", si+1, (int)(v + 0.5f));
                DrawText(buf, (int)inner.x, y, 10, (Color){160,210,140,255}); y += 13;
            }
        }
        break;
    }

    /* ── Contrast ── */
    case 2: {
        /* Level slider */
        GuiLabel((Rectangle){inner.x, y, 50, lh}, "Level:");
        float level = (float)((cs->vmin + cs->vmax) / 2.0);
        float minl = (float)cs->auto_vmin, maxl = (float)cs->auto_vmax;
        float old_level = level;
        GuiSliderBar((Rectangle){inner.x + 52, y, inner.width - 52, lh},
                     NULL, NULL, &level, minl, maxl);
        if (level != old_level) {
            double half = (cs->vmax - cs->vmin) / 2.0;
            cs->vmin = level - half;
            cs->vmax = level + half;
            app->dirty_contrast = 1;
        }
        y += lh + 4;

        /* Width slider */
        GuiLabel((Rectangle){inner.x, y, 50, lh}, "Width:");
        float width_val = (float)(cs->vmax - cs->vmin);
        float old_width = width_val;
        float maxw = (float)(cs->auto_vmax - cs->auto_vmin) * 3.0f;
        if (maxw < 0.01f) maxw = 0.01f;
        GuiSliderBar((Rectangle){inner.x + 52, y, inner.width - 52, lh},
                     NULL, NULL, &width_val, 0.001f * maxw, maxw);
        if (width_val != old_width) {
            double mid = (cs->vmin + cs->vmax) / 2.0;
            cs->vmin = mid - width_val / 2.0;
            cs->vmax = mid + width_val / 2.0;
            app->dirty_contrast = 1;
        }
        y += lh + 8;

        /* Reset button */
        if (GuiButton((Rectangle){inner.x, y, 80, 20}, "Reset")) {
            cs->vmin = cs->auto_vmin;
            cs->vmax = cs->auto_vmax;
            app->dirty_contrast = 1;
        }
        y += 26;

        /* Colormap dropdown — last so it draws over nothing */
        y += 4;
        GuiLabel((Rectangle){inner.x, y, 70, lh}, "Colormap:");
        static bool cmap_edit = false;
        if (GuiDropdownBox((Rectangle){inner.x + 75, y, 100, lh},
                           cmap_names, &cs->cmap, cmap_edit)) {
            cmap_edit = !cmap_edit;
            app->dirty_contrast = 1;
        }
        break;
    }

    /* ── View ── */
    case 3: {
        bool zoom_on = cs->zoom_sync != 0;
        GuiCheckBox((Rectangle){inner.x, y, 16, 16}, "Zoom Sync", &zoom_on);
        if (zoom_on != (cs->zoom_sync != 0)) {
            cs->zoom_sync = zoom_on ? 1 : 0;
            if (cs->zoom_sync)
                cs->zoom = app->zoom;           /* follow global */
            else
                app->zoom = cs->zoom;           /* preserve for re-enable */
        }
        y += 24;

        double disp_zoom = cs->zoom_sync ? app->zoom : cs->zoom;
        snprintf(buf, sizeof(buf), "Zoom: %.2fx", disp_zoom);
        GuiLabel((Rectangle){inner.x, y, inner.width, lh}, buf);
        y += lh + 8;

        if (GuiButton((Rectangle){inner.x, y, 80, 20}, "Screenshot")) {
            TakeScreenshot(
                TextFormat("%s/voxelbase_%04d.png",
                           app->out_dir[0] ? app->out_dir : ".", 0));
        }
        break;
    }

    /* ── Layers ── */
    case 4: {
        int total = cs->num_segs + cs->num_ovls;

        /* header bar */
        DrawRectangleRec((Rectangle){inner.x, y, inner.width, 20}, (Color){42,42,48,255});
        DrawText("Layers", (int)inner.x + 6, y + 3, 10, (Color){160,160,170,255});

        /* +Seg / +Ovl buttons */
        if (cs->num_segs < MAX_SEGS_PER_SLOT) {
            if (GuiButton((Rectangle){inner.x + inner.width - 86, y + 1, 36, 18}, "+Seg"))
                app->pending_attach = 1;
        }
        if (cs->num_ovls < MAX_OVLS_PER_SLOT) {
            if (GuiButton((Rectangle){inner.x + inner.width - 44, y + 1, 36, 18}, "+Ovl"))
                app->pending_attach = 2;
        }
        if (app->pending_attach) {
            DrawText(app->pending_attach == 1 ? "Drop seg file..." : "Drop ovl file...",
                     (int)inner.x + 52, y + 3, 10, (Color){255,200,60,255});
        }

        DrawRectangle((int)inner.x, y + 19, (int)inner.width, 1, (Color){60,60,65,255});
        y += 24;

        if (total == 0) {
            DrawText("No overlays or segmentations",
                     (int)inner.x + 4, y + 8, 10, (Color){100,100,100,255});
            break;
        }

        int row_h = 44;
        int visible_w = (int)inner.width - 8;

        /* ── draw each layer row ── */
        for (int pass = 0; pass < 2; pass++) {
            int count = (pass == 0) ? cs->num_ovls : cs->num_segs;
            for (int li = 0; li < count; li++) {
                Attachment *att = (pass == 0) ? &cs->ovls[li] : &cs->segs[li];
                int is_seg = (pass == 1);
                int this_row_h = is_seg ? 44 : 56;  /* seg: 1 slider, ovl: 2 sliders */
                int ry = y;

                /* row background */
                Color row_bg = is_seg ? (Color){32,38,32,255} : (Color){32,32,38,255};
                DrawRectangleRec((Rectangle){inner.x + 2, ry, visible_w, this_row_h - 2}, row_bg);
                DrawRectangleLinesEx((Rectangle){inner.x + 2, ry, visible_w, this_row_h - 2}, 1,
                                     (Color){50,50,56,255});

                /* ── eye icon ── */
                int ex = (int)inner.x + 10, ey = ry + 14;
                Color eye_c = att->enabled ? (Color){200,210,220,255} : (Color){70,70,75,255};
                /* draw eye shape: outer circle + pupil */
                DrawCircleLines(ex + 7, ey + 1, 6, eye_c);        /* outer */
                if (att->enabled) {
                    DrawCircle(ex + 7, ey + 1, 3, eye_c);          /* pupil */
                } else {
                    /* X across eye when hidden */
                    DrawLine(ex + 2, ey - 4, ex + 12, ey + 6, eye_c);
                }
                /* click detection for eye */
                Vector2 mp = GetMousePosition();
                if (CheckCollisionPointRec(mp, (Rectangle){ex, ey - 5, 16, 16}) &&
                    IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    att->enabled = !att->enabled;
                    app->dirty_contrast = 1;
                }

                /* ── color swatch ── */
                int sx = ex + 20, sy = ry + 12;
                if (is_seg) {
                    /* palette color by index */
                    Color seg_colors[] = {
                        {255,0,0,255},{0,255,0,255},{0,0,255,255},
                        {255,255,0,255},{0,255,255,255},{255,0,255,255},
                        {128,0,0,255},{0,128,0,255},{0,0,128,255},
                        {255,128,0,255},{128,255,0,255},{0,255,128,255},
                    };
                    int ci = li % 12;
                    Color sc = seg_colors[ci];
                    DrawRectangle(sx, sy, 16, 16, sc);
                    DrawRectangleLines(sx, sy, 16, 16, (Color){80,80,80,255});
                } else {
                    /* jet gradient swatch for overlay */
                    for (int gx = 0; gx < 16; gx++) {
                        float t = (float)gx / 15.0f;
                        uint8_t jr, jg, jb;
                        /* inline jet: blue→cyan→green→yellow→red */
                        if (t < 0.125f)      { jr=0; jg=0; jb=(uint8_t)((t+0.125f)/0.25f*255); }
                        else if (t < 0.375f) { jr=0; jg=(uint8_t)((t-0.125f)/0.25f*255); jb=255; }
                        else if (t < 0.625f) { jr=(uint8_t)((t-0.375f)/0.25f*255); jg=255; jb=(uint8_t)((0.625f-t)/0.25f*255); }
                        else if (t < 0.875f) { jr=255; jg=(uint8_t)((0.875f-t)/0.25f*255); jb=0; }
                        else                 { jr=(uint8_t)((1.125f-t)/0.25f*255); jg=0; jb=0; }
                        Color gc = {jr, jg, jb, 255};
                        DrawRectangle(sx + gx, sy, 2, 16, gc);
                    }
                    DrawRectangleLines(sx, sy, 16, 16, (Color){80,80,80,255});
                }

                /* ── layer name + type badge ── */
                int nx = sx + 22;
                /* compute slider position first so we can size the name */
                int sl_x = nx + 36 + 12;  /* badge(~32px) + gap */
                int name_w = sl_x - nx - 6;
                char name_buf[128];
                const char *fname = att->filename;
                int max_chars = name_w / 7;
                if (max_chars < 4) max_chars = 4;
                if ((int)strlen(fname) > max_chars) {
                    snprintf(name_buf, sizeof(name_buf), "%.*s...", max_chars - 3, fname);
                    fname = name_buf;
                }
                DrawText(fname, nx, ry + 3, 10, (Color){210,210,215,255});

                /* type badge */
                const char *badge = is_seg ? "SEG" : "OVL";
                Color badge_c = is_seg ? (Color){80,160,80,255} : (Color){100,100,180,255};
                int bw = MeasureText(badge, 8) + 6;
                DrawRectangle(nx, ry + 22, bw, 13, badge_c);
                DrawText(badge, nx + 3, ry + 22, 8, (Color){20,20,20,255});

                /* ── opacity slider ── */
                sl_x = nx + bw + 12;
                int sl_w = (int)(inner.x + inner.width) - sl_x - 10;

                /* right-click on row to open context menu */
                if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) &&
                    CheckCollisionPointRec(mp, (Rectangle){inner.x + 2, ry, visible_w, this_row_h - 2})) {
                    app->ctx_visible = 1;
                    app->ctx_x = (int)mp.x; app->ctx_y = (int)mp.y;
                    app->ctx_is_seg = is_seg;
                    app->ctx_idx = li;
                }

                if (sl_w > 60) {
                    char pct[8];
                    snprintf(pct, sizeof(pct), "%.1f%%", (double)att->opacity * 100.0);
                    DrawText(pct, sl_x, ry + 5, 9, (Color){160,160,160,255});

                    float op = att->opacity;
                    float old_op = op;
                    GuiSliderBar((Rectangle){sl_x + 44, ry + 7, sl_w - 54, 10},
                                 NULL, NULL, &op, 0.0f, 1.0f);
                    if (op != old_op) {
                        att->opacity = op;
                        app->dirty_contrast = 1;
                    }
                }
                /* ── threshold (overlay) or label toggles (seg) ── */
                if (!is_seg && sl_w > 60) {
                    float thresh_max = app->ovl_abs_max;
                    if (thresh_max < 1.0f) thresh_max = 1.0f;
                    char tbuf[8];
                    snprintf(tbuf, sizeof(tbuf), "T%.0f", (double)att->threshold);
                    DrawText(tbuf, sl_x, ry + 21, 9, (Color){140,140,140,255});
                    float thr = att->threshold;
                    float old_thr = thr;
                    GuiSliderBar((Rectangle){sl_x + 44, ry + 23, sl_w - 54, 10},
                                 NULL, NULL, &thr, 0.0f, thresh_max);
                    if (thr != old_thr) {
                        att->threshold = thr;
                        app->dirty_contrast = 1;
                    }
                } else if (is_seg && att->label_count > 0) {
                    /* label toggles: small colored squares */
                    Color label_colors[] = {
                        {255,0,0,255},{0,255,0,255},{0,0,255,255},
                        {255,255,0,255},{0,255,255,255},{255,0,255,255},
                        {128,0,0,255},{0,128,0,255},{0,0,128,255},
                        {255,128,0,255},{128,255,0,255},{0,255,128,255},
                    };
                    int lx = sl_x, ly = ry + 23;
                    for (int li = 0; li < att->label_count && li < 12; li++) {
                        int lbl = att->labels[li];
                        int on = (att->label_mask & (1u << lbl)) != 0;
                        Color lc = label_colors[(lbl-1) % 12];
                        if (!on) { lc.r /= 4; lc.g /= 4; lc.b /= 4; }
                        DrawRectangle(lx, ly, 14, 14, lc);
                        DrawRectangleLines(lx, ly, 14, 14, on ? (Color){200,200,200,120} : (Color){60,60,60,255});
                        /* click to toggle */
                        Vector2 mp2 = GetMousePosition();
                        if (CheckCollisionPointRec(mp2, (Rectangle){lx, ly, 14, 14}) &&
                            IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            att->label_mask ^= (1u << lbl);
                            app->dirty_contrast = 1;
                        }
                        lx += 18;
                    }
                }

                y += this_row_h;
            }
        }

        /* ── right-click context menu ── */
        if (app->ctx_visible) {
            int mx = app->ctx_x, my = app->ctx_y;
            int mw = 80, mh = 22;
            Rectangle menu_rect = {mx, my, mw, mh};
            DrawRectangleRec(menu_rect, (Color){50,50,55,255});
            DrawRectangleLinesEx(menu_rect, 1, (Color){80,80,85,255});
            if (GuiButton((Rectangle){mx + 4, my + 2, mw - 8, 18}, "Delete")) {
                /* delete the attachment */
                ImageSlot *slot = &app->slots[app->active_slot];
                if (app->ctx_is_seg) {
                    if (app->ctx_idx < slot->num_segs) {
                        nifti_image_free(slot->segs[app->ctx_idx].nim);
                        free(slot->segs[app->ctx_idx].vol);
                        for (int j = app->ctx_idx; j < slot->num_segs - 1; j++)
                            slot->segs[j] = slot->segs[j + 1];
                        memset(&slot->segs[slot->num_segs - 1], 0, sizeof(Attachment));
                        slot->num_segs--;
                        app->dirty_contrast = 1;
                    }
                } else {
                    if (app->ctx_idx < slot->num_ovls) {
                        nifti_image_free(slot->ovls[app->ctx_idx].nim);
                        free(slot->ovls[app->ctx_idx].vol);
                        for (int j = app->ctx_idx; j < slot->num_ovls - 1; j++)
                            slot->ovls[j] = slot->ovls[j + 1];
                        memset(&slot->ovls[slot->num_ovls - 1], 0, sizeof(Attachment));
                        slot->num_ovls--;
                        app->dirty_contrast = 1;
                    }
                }
                app->ctx_visible = 0;
            }
            /* click outside closes menu */
            Vector2 mpm = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
                !CheckCollisionPointRec(mpm, menu_rect))
                app->ctx_visible = 0;
        }

        break;
    }

    /* ── REPL ── */
    case 5: {
        Rectangle repl_rect = {inner.x, (float)y, inner.width, inner.y + inner.height - y};
        draw_repl_panel(app, repl_rect);
        break;
    }

    } /* switch */

    /* ── Timeseries chart at bottom of Cursor tab ── */
    if (app->active_panel_tab == 1 && cs->nt > 1 && cs->ts_valid && cs->ts_data) {
        int margin = 6, title_h = 14;
        int cw = (int)inner.width - 8, ch = 90;
        int chart_bottom = (int)(inner.y + inner.height);
        int chart_top = chart_bottom - ch - 4;
        /* only draw if chart is below text elements */
        if (cw >= 50 && ch >= 30 && chart_top >= y + 4) {
            Rectangle chart = {inner.x + 4, chart_top, cw, ch};

            DrawRectangleRec(chart, (Color){22,22,26,255});
            DrawRectangleLinesEx(chart, 1, (Color){60,60,65,255});

            int px0 = (int)chart.x + margin, py0 = (int)chart.y + margin + title_h;
            int pw = cw - 2 * margin, ph = ch - 2 * margin - title_h;

            if (pw >= 20 && ph >= 10) {
                float tmin = cs->ts_data[0], tmax = cs->ts_data[0];
                for (int t = 1; t < cs->nt; t++) {
                    if (cs->ts_data[t] < tmin) tmin = cs->ts_data[t];
                    if (cs->ts_data[t] > tmax) tmax = cs->ts_data[t];
                }
                float vrange = tmax - tmin;
                if (vrange < 0.0001f) vrange = 1.0f;
                float nt_f = (float)(cs->nt - 1);

                #define VAL2Y(v) (py0 + ph - 1 - (int)(((v) - tmin) / vrange * (float)(ph - 1)))
                #define TIME2X(t) (px0 + (int)((float)(t) / nt_f * (float)(pw - 1)))

                Color line_c = {0, 245, 170, 255};
                for (int i = 0; i < cs->nt - 1; i++) {
                    int x0 = TIME2X(i), y0 = VAL2Y(cs->ts_data[i]);
                    int x1 = TIME2X(i+1), y1 = VAL2Y(cs->ts_data[i+1]);
                    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                    int err = dx + dy, e2;
                    while (1) {
                        if (x0 >= px0 && x0 < px0 + pw && y0 >= py0 && y0 < py0 + ph)
                            DrawPixel(x0, y0, line_c);
                        if (x0 == x1 && y0 == y1) break;
                        e2 = 2 * err;
                        if (e2 >= dy) { err += dy; x0 += sx; }
                        if (e2 <= dx) { err += dx; y0 += sy; }
                    }
                }

                int ctx = TIME2X(cs->ct);
                if (ctx >= px0 && ctx < px0 + pw) {
                    Color mk_c = {255, 70, 50, 255};
                    for (int row = py0; row < py0 + ph; row++)
                        DrawPixel(ctx, row, mk_c);
                }

                char label_buf[32];
                snprintf(label_buf, sizeof(label_buf), "%.0f", tmax);
                DrawText(label_buf, (int)chart.x + 2, py0, 10, (Color){140,140,140,255});
                snprintf(label_buf, sizeof(label_buf), "%.0f", tmin);
                DrawText(label_buf, (int)chart.x + 2, py0 + ph - 12, 10, (Color){140,140,140,255});

                snprintf(label_buf, sizeof(label_buf), "(%d,%d,%d) t=%d/%d",
                         cs->ts_x, cs->ny - cs->ts_y, cs->ts_z, cs->ct + 1, cs->nt);
                int tw2 = MeasureText(label_buf, 10);
                DrawText(label_buf, (int)chart.x + (cw - tw2) / 2, py0 + ph, 10, (Color){160,160,160,255});

                #undef VAL2Y
                #undef TIME2X
            }
        }
    }
}
