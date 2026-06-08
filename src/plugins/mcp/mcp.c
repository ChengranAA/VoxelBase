/*
 *  BareMRI — MCP Server Plugin
 *
 *  Model Context Protocol server over TCP (local only).
 *  Listens on an ephemeral port, broadcasts MCP_PORT=<port> on startup.
 */

#include "core/app.h"
#include "core/plugin.h"
#include "mcp/mcp_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  #define socklen_t int
  #define SHUT_RDWR SD_BOTH
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <fcntl.h>
  #define closesocket close
#endif

#define MCP_BUF_SIZE 4096

/* ── Private plugin state ───────────────────────────────────────── */

typedef struct {
    int listen_fd;
    int conn_fd;
    char rbuf[MCP_BUF_SIZE];
    int rlen;
    char wbuf[MCP_BUF_SIZE];
    int wpos;
    int wsent;
} MCPState;

static int mcp_set_nonblock(int fd) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* ── Response writers ────────────────────────────────────────────── */

static int mcp_write_ok(App *a, MCPState *s) {
    (void)a;
    int n = snprintf(s->wbuf, MCP_BUF_SIZE, "{\"ok\":true}\n");
    s->wpos = (n < MCP_BUF_SIZE) ? n : MCP_BUF_SIZE - 1;
    s->wsent = 0;
    return s->wpos;
}

static int mcp_write_ok_warn(App *a, MCPState *s, const char *fmt, ...) {
    (void)a;
    va_list ap;
    va_start(ap, fmt);
    char *p = s->wbuf;
    char *end = s->wbuf + MCP_BUF_SIZE;
    p += snprintf(p, end - p, "{\"ok\":true,\"warning\":\"");
    if (p > end) p = end;
    p += vsnprintf(p, end - p, fmt, ap);
    if (p > end) p = end;
    p += snprintf(p, end - p, "\"}\n");
    if (p > end) p = end;
    va_end(ap);
    s->wpos = (int)(p - s->wbuf);
    s->wsent = 0;
    return s->wpos;
}

static int mcp_write_err(App *a, MCPState *s, const char *fmt, ...) {
    (void)a;
    va_list ap;
    va_start(ap, fmt);
    char *p = s->wbuf;
    char *end = s->wbuf + MCP_BUF_SIZE;
    p += snprintf(p, end - p, "{\"ok\":false,\"error\":\"");
    if (p > end) p = end;
    p += vsnprintf(p, end - p, fmt, ap);
    if (p > end) p = end;
    p += snprintf(p, end - p, "\"}\n");
    if (p > end) p = end;
    va_end(ap);
    s->wpos = (int)(p - s->wbuf);
    s->wsent = 0;
    return s->wpos;
}

/* ── Command handlers ────────────────────────────────────────────── */

static int mcp_cmd_crosshair(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int x, y, z;
    if (mcp_find_field(p, end, "x", &v) || mcp_read_int(&v, end, &x)) {
        mcp_write_err(a, s, "missing or invalid field: x"); return 0;
    }
    if (mcp_find_field(p, end, "y", &v) || mcp_read_int(&v, end, &y)) {
        mcp_write_err(a, s, "missing or invalid field: y"); return 0;
    }
    if (mcp_find_field(p, end, "z", &v) || mcp_read_int(&v, end, &z)) {
        mcp_write_err(a, s, "missing or invalid field: z"); return 0;
    }
    ImageSlot *cs = &CUR_SLOT(a);
    if (x < 0) x = 0; if (x >= cs->nx) x = cs->nx - 1;
    if (y < 0) y = 0; if (y >= cs->ny) y = cs->ny - 1;
    if (z < 0) z = 0; if (z >= cs->nz) z = cs->nz - 1;
    a->cx = x; a->cy = y; a->cz = z;
    a->ch_fx = cs->nx > 1 ? (double)x / (double)cs->nx : 0.5;
    a->ch_fy = cs->ny > 1 ? (double)y / (double)cs->ny : 0.5;
    a->ch_fz = cs->nz > 1 ? (double)z / (double)cs->nz : 0.5;
    if (cs->nt > 1) {
        cs->ts_x = x; cs->ts_y = y; cs->ts_z = z;
        cs->ts_valid = 1;
        extract_timeseries(a, x, y, z);
    }
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_focus(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; char view[16];
    if (mcp_find_field(p, end, "view", &v) || mcp_read_string(&v, end, view, sizeof(view))) {
        mcp_write_err(a, s, "missing or invalid field: view"); return 0;
    }
    if (strcmp(view, "axial") == 0)      a->focus = 0;
    else if (strcmp(view, "sagittal") == 0) a->focus = 1;
    else if (strcmp(view, "coronal") == 0)  a->focus = 2;
    else { mcp_write_err(a, s, "invalid view: '%s' (use axial/sagittal/coronal)", view); return 0; }
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_nav_slice(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int delta;
    if (mcp_find_field(p, end, "delta", &v) || mcp_read_int(&v, end, &delta)) {
        mcp_write_err(a, s, "missing or invalid field: delta"); return 0;
    }
    if (delta == 0) { mcp_write_ok(a, s); return 0; }
    ImageSlot *cs = &CUR_SLOT(a);
    int *slic; int smax;
    switch (a->focus) {
    case 0: slic = &a->cz; smax = cs->nz; break;
    case 1: slic = &a->cx; smax = cs->nx; break;
    case 2: slic = &a->cy; smax = cs->ny; break;
    default: slic = &a->cz; smax = cs->nz; break;
    }
    int vv = *slic + delta;
    if (vv < 0) vv = 0; if (vv >= smax) vv = smax - 1;
    *slic = vv;
    if (cs->nt > 1) {
        cs->ts_x = a->cx; cs->ts_y = a->cy; cs->ts_z = a->cz;
        cs->ts_valid = 1;
        extract_timeseries(a, a->cx, a->cy, a->cz);
    }
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_switch(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int idx;
    if (mcp_find_field(p, end, "index", &v) || mcp_read_int(&v, end, &idx)) {
        mcp_write_err(a, s, "missing or invalid field: index"); return 0;
    }
    if (idx < 0 || idx >= a->num_slots) {
        mcp_write_err(a, s, "image index %d out of range (0-%d)", idx, a->num_slots - 1);
        return 0;
    }
    if (idx != a->active_slot && a->num_slots > 1) {
        a->active_slot = idx;
        sidebar_scroll_to_active(a);
        ImageSlot *cs = &CUR_SLOT(a);
        a->cx = (int)(a->ch_fx * cs->nx + 0.5);
        a->cy = (int)(a->ch_fy * cs->ny + 0.5);
        a->cz = (int)(a->ch_fz * cs->nz + 0.5);
        if (a->cx >= cs->nx) a->cx = cs->nx - 1;
        if (a->cy >= cs->ny) a->cy = cs->ny - 1;
        if (a->cz >= cs->nz) a->cz = cs->nz - 1;
        if (a->cx < 0) a->cx = 0;
        if (a->cy < 0) a->cy = 0;
        if (a->cz < 0) a->cz = 0;
        if (cs->nt > 1) {
            cs->ts_x = a->cx; cs->ts_y = a->cy; cs->ts_z = a->cz;
            cs->ts_valid = 1;
            extract_timeseries(a, a->cx, a->cy, a->cz);
        }
    }
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_zoom(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double z;
    if (mcp_find_field(p, end, "zoom", &v) || mcp_read_double(&v, end, &z)) {
        mcp_write_err(a, s, "missing or invalid field: zoom"); return 0;
    }
    if (z < UI_ZOOM_MIN || z > UI_ZOOM_MAX) {
        mcp_write_err(a, s, "zoom must be between %.2f and %.2f", UI_ZOOM_MIN, UI_ZOOM_MAX);
        return 0;
    }
    a->zoom = z;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_cmap(App *a, MCPState *s, const char *json, int len) {
    (void)a;
    char *p = (char*)json, *end = (char*)json + len;
    char *v; char name[16];
    if (mcp_find_field(p, end, "name", &v) || mcp_read_string(&v, end, name, sizeof(name))) {
        mcp_write_err(a, s, "missing or invalid field: name"); return 0;
    }
    if (strcmp(name, "gray") == 0)      CUR_SLOT(a).cmap = GF_CMAP_GRAY;
    else if (strcmp(name, "heat") == 0)     CUR_SLOT(a).cmap = GF_CMAP_HEAT;
    else if (strcmp(name, "turbo") == 0)    CUR_SLOT(a).cmap = GF_CMAP_TURBO;
    else if (strcmp(name, "viridis") == 0)  CUR_SLOT(a).cmap = GF_CMAP_VIRIDIS;
    else if (strcmp(name, "inferno") == 0)  CUR_SLOT(a).cmap = GF_CMAP_INFERNO;
    else { mcp_write_err(a, s, "unknown colormap: '%s'", name); return 0; }
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_window(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double w;
    if (mcp_find_field(p, end, "width", &v) || mcp_read_double(&v, end, &w)) {
        mcp_write_err(a, s, "missing or invalid field: width"); return 0;
    }
    ImageSlot *cs = &CUR_SLOT(a);
    if (w <= 0) {
        mcp_write_err(a, s, "window width must be positive");
        return 0;
    }
    double level = (cs->vmin + cs->vmax) * 0.5;
    cs->vmin = level - w * 0.5;
    cs->vmax = level + w * 0.5;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_level(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double level;
    if (mcp_find_field(p, end, "level", &v) || mcp_read_double(&v, end, &level)) {
        mcp_write_err(a, s, "missing or invalid field: level"); return 0;
    }
    ImageSlot *cs = &CUR_SLOT(a);
    double half_w = (cs->vmax - cs->vmin) * 0.5;
    cs->vmin = level - half_w;
    cs->vmax = level + half_w;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_reset_contrast(App *a, MCPState *s, const char *json, int len) {
    (void)json; (void)len;
    ImageSlot *cs = &CUR_SLOT(a);
    cs->vmin = cs->auto_vmin;
    cs->vmax = cs->auto_vmax;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_timepoint(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int t;
    if (mcp_find_field(p, end, "t", &v) || mcp_read_int(&v, end, &t)) {
        mcp_write_err(a, s, "missing or invalid field: t"); return 0;
    }
    ImageSlot *cs = &CUR_SLOT(a);
    if (cs->nt <= 1) { mcp_write_err(a, s, "not a 4D dataset"); return 0; }
    if (t < 0) t = 0; if (t >= cs->nt) t = cs->nt - 1;
    cs->ct = t;
    cs->ts_x = a->cx; cs->ts_y = a->cy; cs->ts_z = a->cz;
    cs->ts_valid = 1;
    extract_timeseries(a, a->cx, a->cy, a->cz);
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_step_tp(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int dir;
    if (mcp_find_field(p, end, "direction", &v) || mcp_read_int(&v, end, &dir)) {
        mcp_write_err(a, s, "missing or invalid field: direction"); return 0;
    }
    ImageSlot *cs = &CUR_SLOT(a);
    if (cs->nt <= 1) { mcp_write_err(a, s, "not a 4D dataset"); return 0; }
    cs->ct += dir;
    while (cs->ct < 0) cs->ct += cs->nt;
    while (cs->ct >= cs->nt) cs->ct -= cs->nt;
    cs->ts_x = a->cx; cs->ts_y = a->cy; cs->ts_z = a->cz;
    cs->ts_valid = 1;
    extract_timeseries(a, a->cx, a->cy, a->cz);
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_ovl_vis(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int show;
    if (mcp_find_field(p, end, "show", &v) || mcp_read_bool(&v, end, &show)) {
        mcp_write_err(a, s, "missing or invalid field: show"); return 0;
    }
    if (!a->ovl_vol) { mcp_write_err(a, s, "no overlay loaded"); return 0; }
    a->ovl_show = show;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_ovl_thresh(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double val;
    if (mcp_find_field(p, end, "value", &v) || mcp_read_double(&v, end, &val)) {
        mcp_write_err(a, s, "missing or invalid field: value"); return 0;
    }
    if (!a->ovl_vol) { mcp_write_err(a, s, "no overlay loaded"); return 0; }
    if (val < UI_OVL_THRESH_MIN || val > UI_OVL_THRESH_MAX) {
        mcp_write_err(a, s, "threshold must be between %.2f and %.2f",
                      UI_OVL_THRESH_MIN, UI_OVL_THRESH_MAX);
        return 0;
    }
    a->ovl_thresh = val;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_ovl_opacity(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double val;
    if (mcp_find_field(p, end, "value", &v) || mcp_read_double(&v, end, &val)) {
        mcp_write_err(a, s, "missing or invalid field: value"); return 0;
    }
    if (!a->ovl_vol) { mcp_write_err(a, s, "no overlay loaded"); return 0; }
    if (val < 0.0) val = 0.0; if (val > 1.0) val = 1.0;
    a->ovl_opacity = val;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_seg_opacity(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; double val;
    if (mcp_find_field(p, end, "value", &v) || mcp_read_double(&v, end, &val)) {
        mcp_write_err(a, s, "missing or invalid field: value"); return 0;
    }
    if (!a->seg_vol) { mcp_write_err(a, s, "no segmentation loaded"); return 0; }
    if (val < 0.0) val = 0.0; if (val > 1.0) val = 1.0;
    a->seg_opacity = val;
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_screenshot(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; int seg = 0;
    /* seg field is optional */
    if (mcp_find_field(p, end, "seg", &v) == 0)
        mcp_read_bool(&v, end, &seg);
    save_focused_view(a, seg);
    mcp_write_ok(a, s);
    return 0;
}

static int mcp_cmd_get_state(App *a, MCPState *s, const char *json, int len) {
    (void)json; (void)len;
    char *p = s->wbuf;
    char *end = s->wbuf + MCP_BUF_SIZE;

    p += snprintf(p, end - p,
        "{\"ok\":true,\"state\":{"
        "\"win_w\":%d,\"win_h\":%d,"
        "\"active_slot\":%d,"
        "\"crosshair\":{\"x\":%d,\"y\":%d,\"z\":%d},"
        "\"focus\":\"%s\","
        "\"zoom\":%.2f,"
        "\"colormap\":\"%s\",",
        a->win_w, a->win_h,
        a->active_slot,
        a->cx, a->cy, a->cz,
        a->focus == 0 ? "axial" : a->focus == 1 ? "sagittal" : "coronal",
        a->zoom,
        CUR_SLOT(a).cmap == GF_CMAP_GRAY ? "gray" :
        CUR_SLOT(a).cmap == GF_CMAP_HEAT ? "heat" :
        CUR_SLOT(a).cmap == GF_CMAP_TURBO ? "turbo" :
        CUR_SLOT(a).cmap == GF_CMAP_VIRIDIS ? "viridis" : "inferno");
    if (p > end) p = end;

    /* slots array */
    p += snprintf(p, end - p, "\"slots\":[");
    if (p > end) p = end;
    for (int i = 0; i < a->num_slots; i++) {
        ImageSlot *cs = &a->slots[i];
        p += snprintf(p, end - p,
            "%s{\"filename\":\"%s\","
            "\"dims\":{\"x\":%d,\"y\":%d,\"z\":%d,\"t\":%d},"
            "\"tr\":%.4f,"
            "\"contrast\":{\"vmin\":%.2f,\"vmax\":%.2f,"
            "\"auto_vmin\":%.2f,\"auto_vmax\":%.2f},"
            "\"timepoint\":%d}",
            i > 0 ? "," : "",
            cs->filename,
            cs->nx, cs->ny, cs->nz, cs->nt,
            cs->tr,
            cs->vmin, cs->vmax,
            cs->auto_vmin, cs->auto_vmax,
            cs->ct);
        if (p > end) p = end;
    }
    p += snprintf(p, end - p, "],");
    if (p > end) p = end;

    /* overlay */
    p += snprintf(p, end - p,
        "\"overlay\":{\"loaded\":%s,\"visible\":%s,"
        "\"threshold\":%.2f,\"opacity\":%.2f,"
        "\"range\":{\"vmin\":%.2f,\"vmax\":%.2f}},",
        a->ovl_vol ? "true" : "false",
        a->ovl_show ? "true" : "false",
        a->ovl_thresh, a->ovl_opacity,
        a->ovl_vmin, a->ovl_vmax);
    if (p > end) p = end;

    /* seg */
    p += snprintf(p, end - p,
        "\"seg\":{\"loaded\":%s,\"opacity\":%.2f},",
        a->seg_vol ? "true" : "false",
        a->seg_opacity);
    if (p > end) p = end;

    /* events */
    p += snprintf(p, end - p, "\"has_events\":%s",
        a->num_events > 0 ? "true" : "false");
    if (p > end) p = end;

    p += snprintf(p, end - p, "}}\n");
    if (p > end) p = end;

    s->wpos = (int)(p - s->wbuf);
    s->wsent = 0;
    return 0;
}

/* ── Dispatch table ───────────────────────────────────────────────── */

static const struct {
    const char *name;
    int (*fn)(App *a, MCPState *s, const char *json, int len);
} mcp_cmd_table[] = {
    {"set_crosshair",        mcp_cmd_crosshair},
    {"set_focus",            mcp_cmd_focus},
    {"navigate_slice",       mcp_cmd_nav_slice},
    {"switch_image",         mcp_cmd_switch},
    {"set_zoom",             mcp_cmd_zoom},
    {"set_colormap",         mcp_cmd_cmap},
    {"set_window",           mcp_cmd_window},
    {"set_level",            mcp_cmd_level},
    {"reset_contrast",       mcp_cmd_reset_contrast},
    {"set_timepoint",        mcp_cmd_timepoint},
    {"step_timepoint",       mcp_cmd_step_tp},
    {"toggle_overlay",      mcp_cmd_ovl_vis},
    {"set_overlay_threshold",mcp_cmd_ovl_thresh},
    {"set_overlay_opacity",  mcp_cmd_ovl_opacity},
    {"set_seg_opacity",      mcp_cmd_seg_opacity},
    {"screenshot",           mcp_cmd_screenshot},
    {"get_state",            mcp_cmd_get_state},
    {NULL, NULL}
};

/* ── Dispatch helper ──────────────────────────────────────────────── */

static void mcp_dispatch(App *a, MCPState *s, const char *json, int len) {
    char *p = (char*)json, *end = (char*)json + len;
    char *v; char cmd[64];
    if (mcp_find_field(p, end, "cmd", &v) || mcp_read_string(&v, end, cmd, sizeof(cmd))) {
        mcp_write_err(a, s, "missing or invalid field: cmd");
        return;
    }
    for (int i = 0; mcp_cmd_table[i].name; i++) {
        if (strcmp(cmd, mcp_cmd_table[i].name) == 0) {
            mcp_cmd_table[i].fn(a, s, json, len);
            return;
        }
    }
    mcp_write_err(a, s, "unknown command: '%s'", cmd);
}

/* ── Forward declare the plugin descriptor (used by hooks for ctx) ── */
extern Plugin mcp_plugin;

/* ── Plugin lifecycle hooks ──────────────────────────────────────── */

static int mcp_plugin_init(struct App *a) {
    (void)a;
    MCPState *s = calloc(1, sizeof(MCPState));
    if (!s) return 0;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) { free(s); return 0; }
#endif

    s->listen_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) {
        fprintf(stderr, "MCP: socket() failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        free(s);
        return 0;
    }

    /* allow immediate rebind after crash */
    {
        int one = 1;
        setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&one, sizeof(one));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* ephemeral */

    if (bind(s->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "MCP: bind() failed\n");
        closesocket(s->listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        free(s);
        return 0;
    }

    /* read back assigned port */
    {
        struct sockaddr_in bound;
        socklen_t blen = sizeof(bound);
        if (getsockname(s->listen_fd, (struct sockaddr*)&bound, &blen) == 0) {
            int port = ntohs(bound.sin_port);
            printf("MCP_PORT=%d\n", port);
            fflush(stdout);
        }
    }

    if (listen(s->listen_fd, 1) < 0) {
        fprintf(stderr, "MCP: listen() failed\n");
        closesocket(s->listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        free(s);
        return 0;
    }

    mcp_set_nonblock(s->listen_fd);
    s->conn_fd = -1;
    s->rlen = 0;
    s->wpos = 0;
    s->wsent = 0;

    mcp_plugin.ctx = s;

    fprintf(stderr, "MCP: listening on localhost (port echoed to stdout)\n");
    return 0;
}

static int mcp_plugin_frame(struct App *a) {
    MCPState *s = (MCPState *)mcp_plugin.ctx;
    if (!s) return 0;

    /* ---- accept ---- */
    if (s->conn_fd < 0 && s->listen_fd >= 0) {
        int fd = (int)accept(s->listen_fd, NULL, NULL);
        if (fd >= 0) {
            if (mcp_set_nonblock(fd) < 0) {
                closesocket(fd);
            } else {
                s->conn_fd = fd;
                s->rlen = 0;
                s->wpos = 0;
                s->wsent = 0;
            }
        } else if (fd < 0) {
#ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK)
#endif
                fprintf(stderr, "MCP: accept() error\n");
        }
    }

    if (s->conn_fd < 0) return 0;

    /* ---- read ---- */
    if (s->rlen < MCP_BUF_SIZE - 1) {
        int n = (int)recv(s->conn_fd,
                          s->rbuf + s->rlen,
                          MCP_BUF_SIZE - 1 - s->rlen, 0);
        if (n > 0) {
            s->rlen += n;
            s->rbuf[s->rlen] = '\0';
        } else if (n == 0 || (n < 0
#ifndef _WIN32
                   && errno != EAGAIN && errno != EWOULDBLOCK
#endif
#ifdef _WIN32
                   && WSAGetLastError() != WSAEWOULDBLOCK
#endif
                   )) {
            /* connection closed or error */
            closesocket(s->conn_fd);
            s->conn_fd = -1;
            s->rlen = 0;
            return 0;
        }
    }

    /* ---- dispatch ---- */
    /* Look for a complete line (terminated by \n) */
    while (s->rlen > 0) {
        char *nl = (char*)memchr(s->rbuf, '\n', s->rlen);
        if (!nl) break;
        *nl = '\0';
        int line_len = (int)(nl - s->rbuf);

        /* parse and dispatch */
        if (s->wsent >= s->wpos && line_len > 0 && s->rbuf[0] == '{') {
            mcp_dispatch(a, s, s->rbuf, line_len);
        }

        /* remove the processed line from the buffer */
        int consumed = line_len + 1;
        memmove(s->rbuf, s->rbuf + consumed,
                s->rlen - consumed);
        s->rlen -= consumed;
    }

    /* buffer full but no complete line — protocol violation */
    if (s->rlen >= MCP_BUF_SIZE - 1) {
        fprintf(stderr, "MCP: client sent oversized line, disconnecting\n");
        closesocket(s->conn_fd);
        s->conn_fd = -1;
        s->rlen = 0;
    }

    /* ---- write ---- */
    if (s->wsent < s->wpos) {
        int n = (int)send(s->conn_fd,
                          s->wbuf + s->wsent,
                          s->wpos - s->wsent, 0);
        if (n > 0) {
            s->wsent += n;
        } else if (n < 0
#ifndef _WIN32
                   && errno != EAGAIN && errno != EWOULDBLOCK
#endif
#ifdef _WIN32
                   && WSAGetLastError() != WSAEWOULDBLOCK
#endif
                   ) {
            closesocket(s->conn_fd);
            s->conn_fd = -1;
            s->rlen = 0;
            s->wpos = 0;
            s->wsent = 0;
        }
    }

    /* ---- reset write buffer when done ---- */
    if (s->wsent >= s->wpos) {
        s->wpos = 0;
        s->wsent = 0;
    }
    return 0;
}

static void mcp_plugin_shutdown(struct App *a) {
    (void)a;
    MCPState *s = (MCPState *)mcp_plugin.ctx;
    if (!s) return;

    if (s->conn_fd >= 0) {
        closesocket(s->conn_fd);
        s->conn_fd = -1;
    }
    if (s->listen_fd >= 0) {
        closesocket(s->listen_fd);
        s->listen_fd = -1;
    }
#ifdef _WIN32
    WSACleanup();
#endif

    free(s);
    mcp_plugin.ctx = NULL;
}

/* ── Plugin descriptor ────────────────────────────────────────────── */

Plugin mcp_plugin = {
    .name = "mcp",
    .ctx = NULL,
    .init = mcp_plugin_init,
    .frame = mcp_plugin_frame,
    .shutdown = mcp_plugin_shutdown,
};
