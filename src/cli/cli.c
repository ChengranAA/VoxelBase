#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli/cli.h"

static void print_help(const char *prog) {
    fprintf(stderr,
        "VoxelBase — tiny C99 NIfTI viewer\n"
        "\n"
        "Usage: %s [options] <file1.nii> [file2.nii ...]\n"
        "\n"
        "Options:\n"
        "  --out <dir>                Screenshot output directory\n"
        "  -s, --seg <seg.nii>        Segmentation overlay\n"
        "  -o, --overlay <ovl.nii>    Statistical overlay\n"
        "  -p, --profile <WxH>        Window size (default: 1200x800)\n"
        "  -h, --help                 Show this help\n",
        prog);
}

static int parse_size(const char *s, int *w, int *h) {
    return sscanf(s, "%dx%d", w, h) == 2;
}

int cli_parse(int argc, char **argv, CliArgs *out) {
    memset(out, 0, sizeof(*out));
    out->win_w = 1200;
    out->win_h = 800;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 2;
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out->out_dir = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seg") == 0) && i + 1 < argc) {
            out->seg_path = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--overlay") == 0) && i + 1 < argc) {
            out->ovl_path = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--profile") == 0) && i + 1 < argc) {
            if (!parse_size(argv[++i], &out->win_w, &out->win_h)) {
                fprintf(stderr, "ERROR: bad profile '%s' (use WxH)\n", argv[i]);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            if (out->num_files >= CLI_MAX_FILES) {
                fprintf(stderr, "ERROR: too many files (max %d)\n", CLI_MAX_FILES);
                return 1;
            }
            out->file_paths[out->num_files++] = argv[i];
        } else {
            fprintf(stderr, "ERROR: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }
    if (out->num_files == 0) {
        fprintf(stderr, "Note: no input files. Start empty, drag-drop .nii files.\n");
        /* return 0 — allow empty startup for drag-drop */
    }
    return 0;
}
