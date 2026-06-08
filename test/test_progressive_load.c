#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define NIFTI_BASE_IMPLEMENTATION
#define NIFTI_ZNZ_IMPLEMENTATION
#define NIFTI_HEADER_IMPLEMENTATION
#define NIFTI_IMAGE_IMPLEMENTATION
#include "nifti/nifti_image.h"

int voxelbase_is_gz_path(const char *path);
size_t voxelbase_raw_data_bytes(const nifti_image *nim);
int voxelbase_should_progressive_load(const nifti_image *nim, const char *path);

static nifti_image make_image(int64_t nvox, int nbyper, int64_t nt) {
    nifti_image nim;
    memset(&nim, 0, sizeof(nim));
    nim.nvox = nvox;
    nim.nbyper = nbyper;
    nim.nt = nt;
    return nim;
}

int main(void) {
    nifti_image large_4d = make_image(600LL * 1024LL * 1024LL, 1, 2);
    nifti_image small_4d = make_image(16LL * 1024LL * 1024LL, 1, 2);
    nifti_image large_3d = make_image(600LL * 1024LL * 1024LL, 1, 1);
    nifti_image invalid = make_image(100, 0, 2);

    assert(voxelbase_is_gz_path("brain.nii.gz") == 1);
    assert(voxelbase_is_gz_path("brain.nii") == 0);
    assert(voxelbase_is_gz_path("brain.img") == 0);

    assert(voxelbase_raw_data_bytes(&large_4d) == 600ULL * 1024ULL * 1024ULL);
    assert(voxelbase_raw_data_bytes(&invalid) == 0);

    assert(voxelbase_should_progressive_load(&large_4d, "bold.nii") == 1);
    assert(voxelbase_should_progressive_load(&large_4d, "bold.img") == 1);
    assert(voxelbase_should_progressive_load(&large_4d, "bold.nii.gz") == 1);
    assert(voxelbase_should_progressive_load(&large_4d, "bold.hdr.gz") == 0);
    assert(voxelbase_should_progressive_load(&small_4d, "small.nii") == 0);
    assert(voxelbase_should_progressive_load(&large_3d, "anat.nii") == 0);
    assert(voxelbase_should_progressive_load(NULL, "missing.nii") == 0);

    return 0;
}
