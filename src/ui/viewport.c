#include "raygui.h"
#include "app.h"

/* fit_texture — compute destination rect preserving aspect ratio */
Rectangle fit_texture(Rectangle avail, int tex_w, int tex_h) {
    float aspect = (float)tex_w / (float)tex_h;
    float avail_aspect = avail.width / avail.height;
    Rectangle dst = avail;

    if (avail_aspect > aspect) {
        dst.width = avail.height * aspect;
        dst.x += (avail.width - dst.width) / 2.0f;
    } else {
        dst.height = avail.width / aspect;
        dst.y += (avail.height - dst.height) / 2.0f;
    }
    return dst;
}

/* viewport_mouse_to_voxel — convert pixel coords to voxel coords */
int viewport_mouse_to_voxel(Vector2 mouse, Rectangle tex_rect,
                             int tex_w, int tex_h, int *vx, int *vy) {
    if (mouse.x < tex_rect.x || mouse.x > tex_rect.x + tex_rect.width ||
        mouse.y < tex_rect.y || mouse.y > tex_rect.y + tex_rect.height)
        return 0;

    float fx = (mouse.x - tex_rect.x) / tex_rect.width;
    float fy = (mouse.y - tex_rect.y) / tex_rect.height;
    *vx = (int)(fx * (float)tex_w);
    *vy = (int)(fy * (float)tex_h);
    if (*vx < 0) *vx = 0; if (*vx >= tex_w) *vx = tex_w - 1;
    if (*vy < 0) *vy = 0; if (*vy >= tex_h) *vy = tex_h - 1;
    return 1;
}
