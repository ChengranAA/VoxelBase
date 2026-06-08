#include <stdio.h>
#include "app.h"

void save_screenshot(App *app) {
    static int counter = 1;
    const char *dir = app->out_dir[0] ? app->out_dir : ".";
    char path[1024];
    snprintf(path, sizeof(path), "%s/baremri_%04d.png", dir, counter++);
    TakeScreenshot(path);
    fprintf(stderr, "Screenshot saved: %s\n", path);
}
