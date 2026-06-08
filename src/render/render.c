#include <math.h>
#include "app.h"

/* 5 colormaps: 0=gray, 1=hot, 2=jet, 3=bone, 4=coolwarm */

static void cmap_gray(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    int v = (int)(t * 255.0f);
    if (v < 0) v = 0; if (v > 255) v = 255;
    *r = *g = *b = (uint8_t)v;
}

static void cmap_hot(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    if (t < 0.33f)      { *r = (uint8_t)(t/0.33f*255); *g = 0; *b = 0; }
    else if (t < 0.66f) { *r = 255; *g = (uint8_t)((t-0.33f)/0.33f*255); *b = 0; }
    else                { *r = 255; *g = 255; *b = (uint8_t)((t-0.66f)/0.34f*255); }
}

static void cmap_jet(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    if (t < 0.125f)      { *r = 0; *g = 0; *b = (uint8_t)((t+0.125f)/0.25f*255); }
    else if (t < 0.375f) { *r = 0; *g = (uint8_t)((t-0.125f)/0.25f*255); *b = 255; }
    else if (t < 0.625f) { *r = (uint8_t)((t-0.375f)/0.25f*255); *g = 255; *b = (uint8_t)((0.625f-t)/0.25f*255); }
    else if (t < 0.875f) { *r = 255; *g = (uint8_t)((0.875f-t)/0.25f*255); *b = 0; }
    else                 { *r = (uint8_t)((1.125f-t)/0.25f*255); *g = 0; *b = 0; }
}

static void cmap_bone(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
    int v = (int)(t * 255.0f);
    if (v < 0) v = 0; if (v > 255) v = 255;
    *r = *g = *b = (uint8_t)v;
    if (t > 0.15f) *g = (uint8_t)(v * 0.85f);
    if (t > 0.4f)  *b = (uint8_t)(v * 0.7f);
}

static void cmap_coolwarm(uint8_t *r, uint8_t *g, uint8_t *b, float t) {
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

typedef void (*cmap_fn)(uint8_t *, uint8_t *, uint8_t *, float);
static cmap_fn cmaps[5] = {cmap_gray, cmap_hot, cmap_jet, cmap_bone, cmap_coolwarm};

static void render_slice_to_rgba(uint8_t *rgba, const float *slice,
                                  int w, int h, double vmin, double vmax,
                                  int cmap_idx) {
    double range = vmax - vmin;
    if (range <= 0.0) range = 1.0;
    cmap_fn cmap = cmaps[cmap_idx];

    for (int i = 0; i < w * h; i++) {
        float t = (float)(((double)slice[i] - vmin) / range);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        uint8_t *p = rgba + i * 4;
        cmap(&p[0], &p[1], &p[2], t);
        p[3] = 255;
    }
}

/* Seg color palette (20 label colors) */
static Color seg_palette[20] = {
    {0,0,0,0},       {255,0,0,255},   {0,255,0,255},   {0,0,255,255},
    {255,255,0,255},  {0,255,255,255}, {255,0,255,255}, {128,0,0,255},
    {0,128,0,255},    {0,0,128,255},   {128,128,0,255}, {0,128,128,255},
    {128,0,128,255},  {255,128,0,255}, {128,255,0,255}, {0,255,128,255},
    {0,128,255,255},  {255,0,128,255}, {128,128,128,255},{192,192,192,255}
};

static void blend_seg_overlay(uint8_t *rgba, const float *seg, int w, int h,
                                double opacity, uint32_t label_mask) {
    for (int i = 0; i < w * h; i++) {
        int label = (int)(seg[i] + 0.5f);
        if (label > 0 && label < 20 && (!label_mask || (label_mask & (1u << label)))) {
            Color c = seg_palette[label];
            uint8_t *p = rgba + i * 4;
            float a = (float)c.a / 255.0f * (float)opacity;
            p[0] = (uint8_t)(p[0] * (1.0f - a) + c.r * a);
            p[1] = (uint8_t)(p[1] * (1.0f - a) + c.g * a);
            p[2] = (uint8_t)(p[2] * (1.0f - a) + c.b * a);
        }
    }
}

static void blend_ovl_overlay(uint8_t *rgba, const float *ovl, int w, int h,
                                double thresh, double opacity) {
    for (int i = 0; i < w * h; i++) {
        float v = ovl[i];
        float av = fabsf(v);
        if (av > (float)thresh) {
            uint8_t *p = rgba + i * 4;
            /* alpha: scales with magnitude, capped at opacity */
            float a = (av / 5.0f) * (float)opacity;
            if (a > (float)opacity) a = (float)opacity;
            /* jet colormap on |v|: blue→cyan→green→yellow→red */
            float t = av / 10.0f;
            if (t > 1.0f) t = 1.0f;
            uint8_t jr, jg, jb;
            cmap_jet(&jr, &jg, &jb, t);
            p[0] = (uint8_t)(p[0] * (1.0f - a) + jr * a);
            p[1] = (uint8_t)(p[1] * (1.0f - a) + jg * a);
            p[2] = (uint8_t)(p[2] * (1.0f - a) + jb * a);
        }
    }
}

void upload_texture(Texture2D *tex, const uint8_t *rgba, int w, int h, int force_recreate) {
    if (w <= 0 || h <= 0 || !rgba) return;
    if (tex->id > 0 && (tex->width != w || tex->height != h || force_recreate)) {
        UnloadTexture(*tex);
        tex->id = 0;
    }
    Image img = { .data = (void *)rgba, .width = w, .height = h,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (tex->id == 0) {
        *tex = LoadTextureFromImage(img);
        SetTextureFilter(*tex, TEXTURE_FILTER_POINT);
    } else {
        UpdateTexture(*tex, rgba);
    }
}

void render_and_upload(App *app, int force_recreate) {
    ImageSlot *cs = &app->slots[app->active_slot];
    double vmin = cs->vmin, vmax = cs->vmax;
    int cmap = cs->cmap;

    /* axial */
    render_slice_to_rgba(app->rgba_axial, app->axial_slice,
                         cs->nx, cs->ny, vmin, vmax, cmap);
    for (int si = 0; si < cs->num_segs; si++) {
        if (cs->segs[si].enabled && cs->segs[si].vol)
            blend_seg_overlay(app->rgba_axial, app->seg_axial_slice,
                              cs->nx, cs->ny, cs->segs[si].opacity, cs->segs[si].label_mask);
    }
    for (int oi = 0; oi < cs->num_ovls; oi++) {
        if (cs->ovls[oi].enabled && cs->ovls[oi].vol)
            blend_ovl_overlay(app->rgba_axial, app->ovl_axial_slice,
                              cs->nx, cs->ny, cs->ovls[oi].threshold, cs->ovls[oi].opacity);
    }
    upload_texture(&app->tex_axial, app->rgba_axial, cs->nx, cs->ny, force_recreate);

    /* sagittal */
    render_slice_to_rgba(app->rgba_sagittal, app->sagittal_slice,
                         cs->ny, cs->nz, vmin, vmax, cmap);
    for (int si = 0; si < cs->num_segs; si++) {
        if (cs->segs[si].enabled && cs->segs[si].vol)
            blend_seg_overlay(app->rgba_sagittal, app->seg_sagittal_slice,
                              cs->ny, cs->nz, cs->segs[si].opacity, cs->segs[si].label_mask);
    }
    for (int oi = 0; oi < cs->num_ovls; oi++) {
        if (cs->ovls[oi].enabled && cs->ovls[oi].vol)
            blend_ovl_overlay(app->rgba_sagittal, app->ovl_sagittal_slice,
                              cs->ny, cs->nz, cs->ovls[oi].threshold, cs->ovls[oi].opacity);
    }
    upload_texture(&app->tex_sagittal, app->rgba_sagittal, cs->ny, cs->nz, force_recreate);

    /* coronal */
    render_slice_to_rgba(app->rgba_coronal, app->coronal_slice,
                         cs->nx, cs->nz, vmin, vmax, cmap);
    for (int si = 0; si < cs->num_segs; si++) {
        if (cs->segs[si].enabled && cs->segs[si].vol)
            blend_seg_overlay(app->rgba_coronal, app->seg_coronal_slice,
                              cs->nx, cs->nz, cs->segs[si].opacity, cs->segs[si].label_mask);
    }
    for (int oi = 0; oi < cs->num_ovls; oi++) {
        if (cs->ovls[oi].enabled && cs->ovls[oi].vol)
            blend_ovl_overlay(app->rgba_coronal, app->ovl_coronal_slice,
                              cs->nx, cs->nz, cs->ovls[oi].threshold, cs->ovls[oi].opacity);
    }
    upload_texture(&app->tex_coronal, app->rgba_coronal, cs->nx, cs->nz, force_recreate);
}
