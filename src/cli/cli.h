#ifndef VOXELBASE_CLI_H
#define VOXELBASE_CLI_H

#define CLI_MAX_FILES 10

typedef struct {
  int num_files;
  const char *file_paths[CLI_MAX_FILES];
  const char *seg_path;
  const char *ovl_path;
  const char *out_dir;
  int win_w, win_h;
} CliArgs;

int cli_parse(int argc, char **argv, CliArgs *out);

#endif
