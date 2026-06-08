/*
   NIFTI_IMAGE.H -- Layer 4: Unified nifti_image and image-level operations

   This is a header-only library (STB-style). To use it:

     #define NIFTI_IMAGE_IMPLEMENTATION
     #include "nifti_image.h"

   Depends on Layer 3 (nifti_header.h), which transitively includes
   Layer 1 (nifti_base.h) and Layer 2 (nifti_znz.h).

   Provides:
     - nifti_image struct (unified for NIfTI-1 and NIfTI-2)
     - Image load / save / free
     - Datatype conversion (with proper scaling)
     - Image creation / cloning
     - Voxel access helpers
     - Brick / subvolume I/O
     - Info display / validation

   PUBLIC DOMAIN - No warranty, use at your own risk.
*/

#ifndef NIFTI_IMAGE_H
#define NIFTI_IMAGE_H

#include "nifti_header.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 *  nifti_image — the unified image struct
 *
 *  This single struct works for both NIfTI-1 and NIfTI-2.  All spatial
 *  fields are stored as double or int64_t so that NIfTI-2 precision is
 *  fully preserved even on builds compiled originally for NIfTI-1.
 * ==================================================================== */
typedef struct {
    int64_t ndim;           /* number of dimensions (1..7)                   */
    int64_t nx, ny, nz;     /* spatial dimensions                           */
    int64_t nt, nu, nv, nw; /* higher dimensions (time, 5th, 6th, 7th)     */
    int64_t nvox;           /* total number of voxels = product of dims     */
    int     nbyper;         /* bytes per voxel                              */
    int     datatype;       /* NIFTI_DT_* code                              */

    /* voxel sizes (double for NIfTI-2 precision) */
    double  dx, dy, dz, dt, du, dv, dw;

    /* scaling */
    double  scl_slope, scl_inter;
    double  cal_min, cal_max;

    /* transform info */
    int     qform_code, sform_code;
    int     freq_dim, phase_dim, slice_dim;
    int     slice_code;
    int64_t slice_start, slice_end;
    double  slice_duration;

    /* quaternion params (double precision) */
    double  quatern_b, quatern_c, quatern_d;
    double  qoffset_x, qoffset_y, qoffset_z;
    double  qfac;

    /* matrices (double precision for NIfTI-2) */
    nifti_dmat44 qto_xyz, qto_ijk;
    nifti_dmat44 sto_xyz, sto_ijk;

    /* time offset */
    double  toffset;
    int     xyz_units, time_units;
    int     nifti_type;     /* NIFTI_FTYPE_*                                */
    int     intent_code;
    double  intent_p1, intent_p2, intent_p3;

    /* file names */
    char   *fname;          /* header file name (.hdr or .nii)              */
    char   *iname;          /* image file name  (.img or .nii)              */
    int64_t iname_offset;   /* byte offset in image file where data starts  */

    /* byte order */
    int     swapsize;
    int     byteorder;      /* NIFTI_LSB_FIRST or NIFTI_MSB_FIRST           */

    /* data */
    void   *data;           /* pointer to voxel data buffer                 */

    /* extensions */
    int     num_ext;
    nifti1_extension *ext_list;
} nifti_image;

/* ====================================================================
 *  DECLARED API FUNCTIONS
 * ==================================================================== */

/* ---- Core I/O ---- */

/* Load a NIfTI file.  If read_data == 0 only the header is parsed.
   Auto-detects NIfTI-1 vs NIfTI-2 and .gz vs plain.
   Returns a malloc'd nifti_image*, or NULL on failure. */
nifti_image *nifti_image_load(const char *fname, int read_data);

/* Free all memory associated with an image (header strings, data,
   extensions, and the struct itself).  Safe to call with NULL. */
void         nifti_image_free(nifti_image *nim);

/* Write an image to a file.  Auto-decides NIfTI-1 vs NIfTI-2 based
   on nim->nifti_type.  Returns 0 on success, non-zero on error. */
int          nifti_image_write(nifti_image *nim, const char *fname);

/* Like nifti_image_write but guaranteed to return 0 on success. */
int          nifti_image_write_status(nifti_image *nim, const char *fname);

/* ---- Datatype conversion ---- */

/* Create a NEW image with all voxel data converted to new_dtype.
   Properly scales by scl_slope / scl_inter when crossing between
   integer and floating-point types.  Returns NULL on failure.
   The caller must free the returned image with nifti_image_free(). */
nifti_image *nifti_image_copy_to_datatype(const nifti_image *src,
                                          int new_dtype);

/* Convert data in-place, replacing the data pointer.  Returns 0 on
   success, non-zero on error. */
int          nifti_image_convert_inplace(nifti_image *nim, int new_dtype);

/* ---- Image creation / cloning ---- */

/* Deep clone: copies header fields, data buffer, extensions, and
   filename strings.  Returns NULL on failure. */
nifti_image *nifti_image_clone(const nifti_image *src);

/* Create a new empty image with given dimensions and datatype.
   dims[0..ndim-1] = dimension sizes.  Allocates data buffer
   (zero-filled).  Returns NULL on failure. */
nifti_image *nifti_image_new(int ndim, const int64_t *dims, int datatype);

/* Convenience: create a simple 3-D or 4-D image.
   Pass nt = 1 for a pure 3-D volume. */
nifti_image *nifti_image_new_simple(int64_t nx, int64_t ny, int64_t nz,
                                    int64_t nt, int datatype);

/* Set voxel spacings (convenience). */
void         nifti_image_set_spacing(nifti_image *nim,
                                     double dx, double dy,
                                     double dz, double dt);

/* Set the origin via qoffset and update qto_xyz / qto_ijk matrices. */
void         nifti_image_set_origin(nifti_image *nim,
                                    double ox, double oy, double oz);

/* ---- Data access helpers ---- */

/* Total data buffer size in bytes. */
size_t       nifti_image_total_bytes(const nifti_image *nim);

/* Get a scalar voxel value as a double (handles all NIfTI datatypes).
   Coordinates are 0-based.  Out-of-range returns 0.0. */
double       nifti_image_get_voxel(const nifti_image *nim,
                                   int64_t i, int64_t j,
                                   int64_t k, int64_t t);

/* Set a scalar voxel value from a double.  Clamps to the datatype's
   representable range when storing to integer types. */
void         nifti_image_set_voxel(nifti_image *nim,
                                   int64_t i, int64_t j,
                                   int64_t k, int64_t t, double val);

/* Fill the entire image with a constant value. */
void         nifti_image_fill(nifti_image *nim, double value);

/* Compute the linear (flat) index from 0-based coordinates.
   Returns -1 if any coordinate is out of range. */
int64_t      nifti_image_voxel_offset(const nifti_image *nim,
                                      int64_t i, int64_t j,
                                      int64_t k, int64_t t);

/* ---- Info / display ---- */

/* Print human-readable image info to stdout. */
void         nifti_image_infodump(const nifti_image *nim);

/* Return 1 if the image struct looks internally consistent. */
int          nifti_image_valid(const nifti_image *nim);

/* ---- Brick / subvolume I/O ---- */

/* Load image data in bricks (sub-volumes).  brick_dims is an array of
   3 or 4 int64_t giving the brick size.  brick_data is an array of
   void* pointers (one per brick) that will be allocated and filled.
   Returns the number of bricks on success, 0 on error. */
int          nifti_image_load_bricks(nifti_image *nim,
                                     const int64_t *brick_dims,
                                     void ***brick_data);

#ifdef __cplusplus
}
#endif

#endif /* NIFTI_IMAGE_H */

/* ====================================================================
 *  IMPLEMENTATION
 * ==================================================================== */
#ifdef NIFTI_IMAGE_IMPLEMENTATION
#ifndef NIFTI_IMAGE_IMPLEMENTED
#define NIFTI_IMAGE_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>

/* ====================================================================
 *  INTERNAL HELPERS — forward declarations
 * ==================================================================== */

static int    image_fill_nifti1_from_header(nifti_image *nim,
                                            const nifti_1_header *hdr);
static int    image_fill_nifti2_from_header(nifti_image *nim,
                                            const nifti_2_header *hdr);
static void   image_build_nifti1_header(const nifti_image *nim,
                                        nifti_1_header *hdr);
static void   image_build_nifti2_header(const nifti_image *nim,
                                        nifti_2_header *hdr);
static void   image_compute_matrices(nifti_image *nim);
static int    image_read_data(const char *fname, int64_t offset,
                              nifti_image *nim);
static int    image_write_single_file(nifti_image *nim, const char *fname);
static int    image_write_hdr_img_pair(nifti_image *nim,
                                       const char *hdr_fname,
                                       const char *img_fname);
static double image_read_voxel_as_double(const void *src, int dtype,
                                         int64_t index);
static void   image_write_voxel_from_double(void *dst, int dtype,
                                            int64_t index, double val);
static void   image_clamp_double_for_dtype(double *val, int dtype);

/* ====================================================================
 *  nifti_image_load
 *
 *  Auto-detects:
 *    - .hdr / .img pairs   (header in .hdr, data in .img)
 *    - .nii                (single file)
 *    - .nii.gz / .hdr.gz   (compressed, via znz)
 *    - NIfTI-1 vs NIfTI-2  (via magic bytes)
 * ==================================================================== */
nifti_image *nifti_image_load(const char *fname, int read_data)
{
    nifti_image *nim = NULL;
    int    is_nifti2 = 0;
    int    is_hdr_img = 0;
    int    needs_swap = 0;
    char   hdr_fname[4096];
    char   img_fname[4096];
    char   base[4096];
    size_t flen;

    if (!fname || !fname[0]) return NULL;

    /* ---- allocate image struct and zero it ---- */
    nim = (nifti_image *)calloc(1, sizeof(nifti_image));
    if (!nim) return NULL;

    /* reasonable defaults */
    nim->scl_slope  = 1.0;
    nim->scl_inter  = 0.0;
    nim->qfac       = 1.0;
    nim->byteorder  = nifti_short_order();
    nim->qto_xyz    = nifti_dmat44_identity();
    nim->qto_ijk    = nifti_dmat44_identity();
    nim->sto_xyz    = nifti_dmat44_identity();
    nim->sto_ijk    = nifti_dmat44_identity();

    flen = strlen(fname);

    /* ---- determine file type from extension ---- */
    if (flen >= 4) {
        const char *ext = fname + flen - 4;
        if (strcmp(ext, ".hdr") == 0 || strcmp(ext, ".HDR") == 0) {
            is_hdr_img = 1;
        }
    }
    if (flen >= 7) {
        const char *ext = fname + flen - 7;
        if (strcmp(ext, ".hdr.gz") == 0 || strcmp(ext, ".HDR.GZ") == 0) {
            is_hdr_img = 1;
        }
    }

    /* ---- determine version by peeking at sizeof_hdr ---- */
    {
        znzFile fp;
        int     use_comp;

        use_comp = nifti_is_gzfile(fname) ? 1 : 0;

        if (is_hdr_img) {
            /* build image file name from header file name */
            if (flen >= 7 &&
                (strcmp(fname + flen - 7, ".hdr.gz") == 0 ||
                 strcmp(fname + flen - 7, ".HDR.GZ") == 0)) {
                /* fname ends with .hdr.gz → replace with .img.gz */
                memcpy(base, fname, flen - 7);
                base[flen - 7] = '\0';
                snprintf(img_fname, sizeof(img_fname), "%s.img.gz", base);
            } else {
                /* fname ends with .hdr → replace with .img */
                memcpy(base, fname, flen - 4);
                base[flen - 4] = '\0';
                snprintf(img_fname, sizeof(img_fname), "%s.img", base);
            }
            snprintf(hdr_fname, sizeof(hdr_fname), "%s", fname);
        } else {
            snprintf(img_fname, sizeof(img_fname), "%s", fname);
            snprintf(hdr_fname, sizeof(hdr_fname), "%s", fname);
        }

        /* peek at sizeof_hdr to determine NIfTI-1 vs NIfTI-2 */
        fp = znzopen(hdr_fname, "rb", use_comp);
        if (!fp) {
            free(nim);
            return NULL;
        }
        {
            int32_t peek = 0;
            if (znzread(&peek, 4, 1, fp) != 1) {
                znzclose(&fp);
                free(nim);
                return NULL;
            }
            znzrewind(fp);

            if (peek == 348 || peek == 1543569408) {
                is_nifti2 = 0;
            } else if (peek == 540 || peek == 469893120) {
                is_nifti2 = 1;
            } else {
                znzclose(&fp);
                free(nim);
                return NULL;
            }
        }
        znzclose(&fp);
    }

    /* ---- read the on-disk header ---- */
    if (is_nifti2) {
        nifti_2_header *h2;
        h2 = nifti_read_header_2(hdr_fname, &needs_swap);
        if (!h2) {
            free(nim);
            return NULL;
        }
        image_fill_nifti2_from_header(nim, h2);
        free(h2);
    } else {
        nifti_1_header *h1;
        h1 = nifti_read_header_1(hdr_fname, &needs_swap);
        if (!h1) {
            free(nim);
            return NULL;
        }
        image_fill_nifti1_from_header(nim, h1);
        free(h1);
    }

    /* ---- record file names ---- */
    nim->fname = nifti_strdup(hdr_fname);
    nim->iname = nifti_strdup(img_fname);

    /* ---- byte order ---- */
    if (needs_swap) {
        nim->byteorder = (nifti_short_order() == NIFTI_LSB_FIRST)
                         ? NIFTI_MSB_FIRST : NIFTI_LSB_FIRST;
    }

    /* ---- read extensions (only for .nii / .nii.gz, not .hdr/.img) ---- */
    if (!is_hdr_img) {
        nifti_read_extensions(hdr_fname, &nim->num_ext, &nim->ext_list);
    }

    /* ---- compute transform matrices ---- */
    image_compute_matrices(nim);

    /* ---- read voxel data if requested ---- */
    if (read_data) {
        if (image_read_data(img_fname, nim->iname_offset, nim) != 0) {
            /* reading data failed — still return the header, but
               data will be NULL.  Caller can check nim->data. */
        }
    }

    return nim;
}

/* ====================================================================
 *  nifti_image_free
 * ==================================================================== */
void nifti_image_free(nifti_image *nim)
{
    if (!nim) return;

    free(nim->fname);
    free(nim->iname);
    free(nim->data);

    if (nim->num_ext > 0 && nim->ext_list) {
        nifti_free_extensions(nim->num_ext, nim->ext_list);
    }

    free(nim);
}

/* ====================================================================
 *  nifti_image_write
 * ==================================================================== */
int nifti_image_write(nifti_image *nim, const char *fname)
{
    int is_hdr_img = 0;
    char hdr_fname[4096];
    char img_fname[4096];
    size_t flen;

    if (!nim || !fname || !fname[0]) return -1;

    flen = strlen(fname);

    if (flen >= 4) {
        const char *ext = fname + flen - 4;
        if (strcmp(ext, ".hdr") == 0 || strcmp(ext, ".HDR") == 0) {
            is_hdr_img = 1;
        }
    }
    if (flen >= 7) {
        const char *ext = fname + flen - 7;
        if (strcmp(ext, ".hdr.gz") == 0 || strcmp(ext, ".HDR.GZ") == 0) {
            is_hdr_img = 1;
        }
    }

    if (is_hdr_img) {
        /* ---- .hdr / .img pair ---- */
        snprintf(hdr_fname, sizeof(hdr_fname), "%s", fname);

        if (flen >= 7 &&
            (strcmp(fname + flen - 7, ".hdr.gz") == 0 ||
             strcmp(fname + flen - 7, ".HDR.GZ") == 0)) {
            memcpy(img_fname, fname, flen - 7);
            img_fname[flen - 7] = '\0';
            /* append .img.gz */
            strncat(img_fname, ".img.gz",
                    sizeof(img_fname) - strlen(img_fname) - 1);
        } else {
            memcpy(img_fname, fname, flen - 4);
            img_fname[flen - 4] = '\0';
            strncat(img_fname, ".img",
                    sizeof(img_fname) - strlen(img_fname) - 1);
        }

        return image_write_hdr_img_pair(nim, hdr_fname, img_fname);
    } else {
        /* ---- single .nii file ---- */
        return image_write_single_file(nim, fname);
    }
}

/* ====================================================================
 *  nifti_image_write_status
 * ==================================================================== */
int nifti_image_write_status(nifti_image *nim, const char *fname)
{
    return nifti_image_write(nim, fname);
}

/* ====================================================================
 *  nifti_image_copy_to_datatype
 *
 *  The KEY utility.  Converts voxel data to a new datatype, properly
 *  handling scaling:
 *
 *    integer → float :  real = stored * scl_slope + scl_inter
 *    float → integer :  stored = round((real - scl_inter) / scl_slope)
 *    integer → integer : keep slope/inter, range-cast
 *    float → float : just cast, set slope = 1, inter = 0
 * ==================================================================== */
nifti_image *nifti_image_copy_to_datatype(const nifti_image *src,
                                          int new_dtype)
{
    nifti_image *dst;
    int      src_is_int, dst_is_int;
    int64_t  ii;

    if (!src) return NULL;
    if (!nifti_is_valid_datatype(new_dtype)) return NULL;
    if (!nifti_is_valid_datatype(src->datatype)) return NULL;

    /* ---- create output image via clone, then replace data ---- */
    dst = nifti_image_clone(src);
    if (!dst) return NULL;

    /* free the cloned data buffer — we'll allocate a fresh one */
    free(dst->data);
    dst->data = NULL;

    dst->datatype = new_dtype;
    dst->nbyper   = nifti_datatype_bytes(new_dtype);

    if (dst->nvox <= 0) {
        /* no voxels — nothing to convert */
        dst->scl_slope = 1.0;
        dst->scl_inter = 0.0;
        return dst;
    }

    dst->data = calloc((size_t)dst->nvox, (size_t)dst->nbyper);
    if (!dst->data) {
        nifti_image_free(dst);
        return NULL;
    }

    src_is_int = nifti_is_inttype(src->datatype);
    dst_is_int = nifti_is_inttype(new_dtype);

    for (ii = 0; ii < src->nvox; ii++) {
        double val = image_read_voxel_as_double(
                         src->data, src->datatype, ii);

        if (src_is_int && !dst_is_int) {
            /* integer → float: compute real value */
            val = val * src->scl_slope + src->scl_inter;
        } else if (!src_is_int && dst_is_int) {
            /* float → integer: reverse scaling */
            val = (val - src->scl_inter) / src->scl_slope;
        }
        /* integer → integer or float → float: val is already correct */

        image_clamp_double_for_dtype(&val, new_dtype);
        image_write_voxel_from_double(dst->data, new_dtype, ii, val);
    }

    /* ---- fix up scaling fields ---- */
    if (!dst_is_int) {
        /* for float targets, scaling is baked in */
        dst->scl_slope = 1.0;
        dst->scl_inter = 0.0;
    }
    /* for int targets, keep the slope/inter from source */

    return dst;
}

/* ====================================================================
 *  nifti_image_convert_inplace
 * ==================================================================== */
int nifti_image_convert_inplace(nifti_image *nim, int new_dtype)
{
    nifti_image *tmp;

    if (!nim) return -1;
    if (!nim->data) return -1;

    tmp = nifti_image_copy_to_datatype(nim, new_dtype);
    if (!tmp) return -1;

    /* swap data pointers */
    free(nim->data);
    nim->data      = tmp->data;
    tmp->data      = NULL;   /* so tmp's free doesn't free it */
    nim->datatype  = tmp->datatype;
    nim->nbyper    = tmp->nbyper;
    nim->scl_slope = tmp->scl_slope;
    nim->scl_inter = tmp->scl_inter;

    nifti_image_free(tmp);
    return 0;
}

/* ====================================================================
 *  nifti_image_clone
 * ==================================================================== */
nifti_image *nifti_image_clone(const nifti_image *src)
{
    nifti_image *dst;
    size_t  data_bytes;

    if (!src) return NULL;

    dst = (nifti_image *)malloc(sizeof(nifti_image));
    if (!dst) return NULL;

    /* shallow copy the whole struct */
    memcpy(dst, src, sizeof(nifti_image));

    /* now deep-copy the owned pointers */
    dst->fname = src->fname ? nifti_strdup(src->fname) : NULL;
    dst->iname = src->iname ? nifti_strdup(src->iname) : NULL;

    /* deep-copy data */
    if (src->data && src->nvox > 0 && src->nbyper > 0) {
        data_bytes = (size_t)src->nvox * (size_t)src->nbyper;
        dst->data = malloc(data_bytes);
        if (!dst->data) {
            nifti_image_free(dst);
            return NULL;
        }
        memcpy(dst->data, src->data, data_bytes);
    } else {
        dst->data = NULL;
    }

    /* deep-copy extensions */
    if (src->num_ext > 0 && src->ext_list) {
        int i;
        dst->ext_list = (nifti1_extension *)calloc(
                            (size_t)src->num_ext,
                            sizeof(nifti1_extension));
        if (!dst->ext_list) {
            nifti_image_free(dst);
            return NULL;
        }
        for (i = 0; i < src->num_ext; i++) {
            dst->ext_list[i].esize = src->ext_list[i].esize;
            dst->ext_list[i].ecode = src->ext_list[i].ecode;
            if (src->ext_list[i].esize > 8 &&
                src->ext_list[i].edata) {
                int dlen = src->ext_list[i].esize - 8;
                dst->ext_list[i].edata = (char *)malloc((size_t)dlen);
                if (!dst->ext_list[i].edata) {
                    nifti_image_free(dst);
                    return NULL;
                }
                memcpy(dst->ext_list[i].edata,
                       src->ext_list[i].edata, (size_t)dlen);
            } else {
                dst->ext_list[i].edata = NULL;
            }
        }
    } else {
        dst->ext_list = NULL;
        dst->num_ext  = 0;
    }

    return dst;
}

/* ====================================================================
 *  nifti_image_new
 * ==================================================================== */
nifti_image *nifti_image_new(int ndim, const int64_t *dims, int datatype)
{
    nifti_image *nim;
    int c;

    if (ndim < 1 || ndim > 7) return NULL;
    if (!dims) return NULL;
    if (!nifti_is_valid_datatype(datatype)) return NULL;

    for (c = 0; c < ndim; c++) {
        if (dims[c] <= 0) return NULL;
    }

    nim = (nifti_image *)calloc(1, sizeof(nifti_image));
    if (!nim) return NULL;

    nim->ndim  = ndim;
    nim->nx    = dims[0];
    nim->ny    = (ndim >= 2) ? dims[1] : 1;
    nim->nz    = (ndim >= 3) ? dims[2] : 1;
    nim->nt    = (ndim >= 4) ? dims[3] : 1;
    nim->nu    = (ndim >= 5) ? dims[4] : 1;
    nim->nv    = (ndim >= 6) ? dims[5] : 1;
    nim->nw    = (ndim >= 7) ? dims[6] : 1;

    nim->nvox = nim->nx;
    if (ndim >= 2) nim->nvox *= nim->ny;
    if (ndim >= 3) nim->nvox *= nim->nz;
    if (ndim >= 4) nim->nvox *= nim->nt;
    if (ndim >= 5) nim->nvox *= nim->nu;
    if (ndim >= 6) nim->nvox *= nim->nv;
    if (ndim >= 7) nim->nvox *= nim->nw;

    nim->datatype   = datatype;
    nim->nbyper     = nifti_datatype_bytes(datatype);
    nim->swapsize   = nifti_datatype_swapsize(datatype);
    nim->byteorder  = nifti_short_order();

    nim->dx = nim->dy = nim->dz = nim->dt = 1.0;
    nim->du = nim->dv = nim->dw = 1.0;
    nim->scl_slope = 1.0;
    nim->scl_inter = 0.0;
    nim->qfac      = 1.0;

    /* allocate data buffer (zero-filled by calloc within the calloc) */
    /* But we used calloc on the struct, so data is NULL so far. */
    nim->data = calloc((size_t)nim->nvox, (size_t)nim->nbyper);
    if (!nim->data) {
        free(nim);
        return NULL;
    }

    /* identity matrices */
    nim->qto_xyz = nifti_dmat44_identity();
    nim->qto_ijk = nifti_dmat44_identity();
    nim->sto_xyz = nifti_dmat44_identity();
    nim->sto_ijk = nifti_dmat44_identity();

    return nim;
}

/* ====================================================================
 *  nifti_image_new_simple
 * ==================================================================== */
nifti_image *nifti_image_new_simple(int64_t nx, int64_t ny,
                                    int64_t nz, int64_t nt,
                                    int datatype)
{
    int64_t dims[4];
    int ndim = 4;

    if (nx <= 0 || ny <= 0 || nz <= 0 || nt <= 0) return NULL;

    dims[0] = nx;
    dims[1] = ny;
    dims[2] = nz;
    dims[3] = nt;

    /* if nt == 1, we can reduce to 3-D for cleanliness */
    if (nt == 1) ndim = 3;

    return nifti_image_new(ndim, dims, datatype);
}

/* ====================================================================
 *  nifti_image_set_spacing
 * ==================================================================== */
void nifti_image_set_spacing(nifti_image *nim,
                             double dx, double dy,
                             double dz, double dt)
{
    if (!nim) return;
    nim->dx = dx;
    nim->dy = dy;
    nim->dz = dz;
    nim->dt = dt;
    /* rebuild matrices so pixel sizes are reflected */
    image_compute_matrices(nim);
}

/* ====================================================================
 *  nifti_image_set_origin
 * ==================================================================== */
void nifti_image_set_origin(nifti_image *nim,
                            double ox, double oy, double oz)
{
    if (!nim) return;
    nim->qoffset_x = ox;
    nim->qoffset_y = oy;
    nim->qoffset_z = oz;
    image_compute_matrices(nim);
}

/* ====================================================================
 *  nifti_image_total_bytes
 * ==================================================================== */
size_t nifti_image_total_bytes(const nifti_image *nim)
{
    if (!nim || nim->nvox <= 0 || nim->nbyper <= 0) return 0;
    return (size_t)nim->nvox * (size_t)nim->nbyper;
}

/* ====================================================================
 *  nifti_image_voxel_offset
 *
 *  Layout in memory: i is fastest-varying (x), then j (y), then k (z),
 *  then t, u, v, w.  This is the standard NIfTI / "neurological"
 *  convention (first dimension is the fastest-varying in linear
 *  memory).
 * ==================================================================== */
int64_t nifti_image_voxel_offset(const nifti_image *nim,
                                  int64_t i, int64_t j,
                                  int64_t k, int64_t t)
{
    if (!nim) return -1;
    if (i < 0 || i >= nim->nx) return -1;
    if (j < 0 || j >= nim->ny) return -1;
    if (k < 0 || k >= nim->nz) return -1;
    if (t < 0 || t >= nim->nt) return -1;

    /* i fastest, then j, k, t */
    return (int64_t)(i +
           j * nim->nx +
           k * nim->nx * nim->ny +
           t * nim->nx * nim->ny * nim->nz);
}

/* ====================================================================
 *  nifti_image_get_voxel
 * ==================================================================== */
double nifti_image_get_voxel(const nifti_image *nim,
                              int64_t i, int64_t j,
                              int64_t k, int64_t t)
{
    int64_t offset;

    if (!nim || !nim->data) return 0.0;

    offset = nifti_image_voxel_offset(nim, i, j, k, t);
    if (offset < 0) return 0.0;

    return image_read_voxel_as_double(nim->data, nim->datatype, offset);
}

/* ====================================================================
 *  nifti_image_set_voxel
 * ==================================================================== */
void nifti_image_set_voxel(nifti_image *nim,
                            int64_t i, int64_t j,
                            int64_t k, int64_t t, double val)
{
    int64_t offset;

    if (!nim || !nim->data) return;

    offset = nifti_image_voxel_offset(nim, i, j, k, t);
    if (offset < 0) return;

    image_clamp_double_for_dtype(&val, nim->datatype);
    image_write_voxel_from_double(nim->data, nim->datatype, offset, val);
}

/* ====================================================================
 *  nifti_image_fill
 * ==================================================================== */
void nifti_image_fill(nifti_image *nim, double value)
{
    int64_t ii;

    if (!nim || !nim->data || nim->nvox <= 0) return;

    image_clamp_double_for_dtype(&value, nim->datatype);

    for (ii = 0; ii < nim->nvox; ii++) {
        image_write_voxel_from_double(
            nim->data, nim->datatype, ii, value);
    }
}

/* ====================================================================
 *  nifti_image_infodump
 * ==================================================================== */
void nifti_image_infodump(const nifti_image *nim)
{
    if (!nim) {
        printf("nifti_image_infodump: (null)\n");
        return;
    }

    printf("-----------------------------------------------------------\n");
    printf("nifti_image info:\n");
    printf("  ndim         = %lld\n", (long long)nim->ndim);
    printf("  dimensions   = %lld x %lld x %lld x %lld",
           (long long)nim->nx, (long long)nim->ny,
           (long long)nim->nz, (long long)nim->nt);
    if (nim->ndim >= 5) printf(" x %lld", (long long)nim->nu);
    if (nim->ndim >= 6) printf(" x %lld", (long long)nim->nv);
    if (nim->ndim >= 7) printf(" x %lld", (long long)nim->nw);
    printf("\n");
    printf("  nvox         = %lld\n", (long long)nim->nvox);
    printf("  datatype     = %d (%s)\n", nim->datatype,
           nifti_datatype_string(nim->datatype));
    printf("  nbyper       = %d\n", nim->nbyper);
    printf("  voxel sizes  = %.4f x %.4f x %.4f x %.4f\n",
           nim->dx, nim->dy, nim->dz, nim->dt);
    printf("  scl_slope    = %g\n", nim->scl_slope);
    printf("  scl_inter    = %g\n", nim->scl_inter);
    printf("  cal_min/max  = %g / %g\n", nim->cal_min, nim->cal_max);
    printf("  qform_code   = %d (%s)\n", nim->qform_code,
           nifti_xform_string(nim->qform_code));
    printf("  sform_code   = %d (%s)\n", nim->sform_code,
           nifti_xform_string(nim->sform_code));
    printf("  quatern      = b=%.6f c=%.6f d=%.6f\n",
           nim->quatern_b, nim->quatern_c, nim->quatern_d);
    printf("  qoffset      = %.4f %.4f %.4f\n",
           nim->qoffset_x, nim->qoffset_y, nim->qoffset_z);
    printf("  qfac         = %g\n", nim->qfac);
    printf("  freq/phase/slice = %d / %d / %d\n",
           nim->freq_dim, nim->phase_dim, nim->slice_dim);
    printf("  slice_code   = %d (%s)\n", nim->slice_code,
           nifti_slice_string(nim->slice_code));
    printf("  slice_start  = %lld\n", (long long)nim->slice_start);
    printf("  slice_end    = %lld\n", (long long)nim->slice_end);
    printf("  slice_dur    = %g\n", nim->slice_duration);
    printf("  toffset      = %g\n", nim->toffset);
    printf("  xyz_units    = %d  time_units = %d\n",
           nim->xyz_units, nim->time_units);
    printf("  intent_code  = %d (%s)\n", nim->intent_code,
           nifti_intent_string(nim->intent_code));
    printf("  intent_p1..3 = %g, %g, %g\n",
           nim->intent_p1, nim->intent_p2, nim->intent_p3);
    printf("  nifti_type   = %d", nim->nifti_type);
    if (nim->nifti_type == NIFTI_FTYPE_NIFTI1_1)
        printf(" (NIfTI-1 .hdr/.img)");
    else if (nim->nifti_type == NIFTI_FTYPE_NIFTI1_2)
        printf(" (NIfTI-1 .nii)");
    else if (nim->nifti_type == NIFTI_FTYPE_NIFTI2_1)
        printf(" (NIfTI-2 .hdr/.img)");
    else if (nim->nifti_type == NIFTI_FTYPE_NIFTI2_2)
        printf(" (NIfTI-2 .nii)");
    printf("\n");
    printf("  byteorder    = %s\n",
           nim->byteorder == NIFTI_LSB_FIRST ? "LSB_FIRST" : "MSB_FIRST");
    printf("  data         = %s\n", nim->data ? "present" : "NULL");
    printf("  extensions   = %d\n", nim->num_ext);
    printf("  fname        = %s\n", nim->fname ? nim->fname : "(null)");
    printf("  iname        = %s\n", nim->iname ? nim->iname : "(null)");
    printf("-----------------------------------------------------------\n");
}

/* ====================================================================
 *  nifti_image_valid
 * ==================================================================== */
int nifti_image_valid(const nifti_image *nim)
{
    if (!nim) return 0;

    if (nim->ndim < 1 || nim->ndim > 7) return 0;
    if (nim->nx <= 0 || nim->ny <= 0 || nim->nz <= 0) return 0;
    if (nim->nt <= 0 || nim->nu <= 0 || nim->nv <= 0 || nim->nw <= 0)
        return 0;

    if (!nifti_is_valid_datatype(nim->datatype)) return 0;
    if (nim->nbyper <= 0) return 0;

    /* nvox must match product */
    {
        int64_t prod = nim->nx;
        if (nim->ndim >= 2) prod *= nim->ny;
        if (nim->ndim >= 3) prod *= nim->nz;
        if (nim->ndim >= 4) prod *= nim->nt;
        if (nim->ndim >= 5) prod *= nim->nu;
        if (nim->ndim >= 6) prod *= nim->nv;
        if (nim->ndim >= 7) prod *= nim->nw;
        if (prod != nim->nvox) return 0;
    }

    /* byte order must be one of the two known values */
    if (nim->byteorder != NIFTI_LSB_FIRST &&
        nim->byteorder != NIFTI_MSB_FIRST) return 0;

    return 1;
}

/* ====================================================================
 *  nifti_image_load_bricks
 *
 *  Brick-loading: divides the image into sub-volumes of size
 *  brick_dims[0..2] (and optionally brick_dims[3] for t).
 *  Allocates an array of void* pointers (one per brick) and reads
 *  each brick's data.
 *
 *  brick_data is an output parameter: *brick_data is set to a
 *  malloc'd array of void* pointers.
 *
 *  Returns the number of bricks on success, or 0 on error.
 * ==================================================================== */
int nifti_image_load_bricks(nifti_image *nim,
                            const int64_t *brick_dims,
                            void ***brick_data)
{
    int64_t bx, by, bz, bt;
    int64_t nx_bricks, ny_bricks, nz_bricks, nt_bricks;
    int64_t nbricks;
    int b;
    int64_t ix, iy, iz, it;

    if (!nim || !brick_dims || !brick_data) return 0;

    *brick_data = NULL;

    bx = brick_dims[0];
    by = brick_dims[1];
    bz = brick_dims[2];
    bt = (nim->ndim >= 4) ? brick_dims[3] : 1;

    if (bx <= 0 || by <= 0 || bz <= 0 || bt <= 0) return 0;

    nx_bricks = (nim->nx + bx - 1) / bx;
    ny_bricks = (nim->ny + by - 1) / by;
    nz_bricks = (nim->nz + bz - 1) / bz;
    nt_bricks = (nim->nt + bt - 1) / bt;
    nbricks   = nx_bricks * ny_bricks * nz_bricks * nt_bricks;

    if (nbricks <= 0 || nbricks > 1000000) return 0;

    *brick_data = (void **)calloc((size_t)nbricks, sizeof(void *));
    if (!*brick_data) return 0;

    b = 0;
    for (it = 0; it < nt_bricks; it++) {
        int64_t t_start = it * bt;
        int64_t t_end   = (it + 1) * bt;
        if (t_end > nim->nt) t_end = nim->nt;
        int64_t t_len = t_end - t_start;

        for (iz = 0; iz < nz_bricks; iz++) {
            int64_t z_start = iz * bz;
            int64_t z_end   = (iz + 1) * bz;
            if (z_end > nim->nz) z_end = nim->nz;
            int64_t z_len = z_end - z_start;

            for (iy = 0; iy < ny_bricks; iy++) {
                int64_t y_start = iy * by;
                int64_t y_end   = (iy + 1) * by;
                if (y_end > nim->ny) y_end = nim->ny;
                int64_t y_len = y_end - y_start;

                for (ix = 0; ix < nx_bricks; ix++) {
                    int64_t x_start = ix * bx;
                    int64_t x_end   = (ix + 1) * bx;
                    if (x_end > nim->nx) x_end = nim->nx;
                    int64_t x_len = x_end - x_start;

                    /* allocate brick buffer */
                    int64_t brick_vox = x_len * y_len * z_len * t_len;
                    size_t  brick_bytes;

                    if (brick_vox <= 0) continue;

                    brick_bytes = (size_t)brick_vox *
                                  (size_t)nim->nbyper;
                    (*brick_data)[b] = malloc(brick_bytes);
                    if (!(*brick_data)[b]) {
                        /* free previously allocated bricks */
                        int k;
                        for (k = 0; k < b; k++) {
                            free((*brick_data)[k]);
                        }
                        free(*brick_data);
                        *brick_data = NULL;
                        return 0;
                    }

                    /* copy data from main buffer */
                    {
                        int64_t ti, zi, yi;
                        size_t  offset = 0;
                        for (ti = 0; ti < t_len; ti++) {
                            for (zi = 0; zi < z_len; zi++) {
                                for (yi = 0; yi < y_len; yi++) {
                                    int64_t src_off =
                                        (t_start + ti) * nim->nz *
                                            nim->ny * nim->nx +
                                        (z_start + zi) * nim->ny *
                                            nim->nx +
                                        (y_start + yi) * nim->nx +
                                        x_start;
                                    size_t row_bytes =
                                        (size_t)x_len *
                                        (size_t)nim->nbyper;

                                    if (nim->data) {
                                        memcpy(
                                            (char *)(*brick_data)[b]
                                                + offset,
                                            (const char *)nim->data +
                                                src_off * nim->nbyper,
                                            row_bytes);
                                    } else {
                                        memset(
                                            (char *)(*brick_data)[b]
                                                + offset,
                                            0, row_bytes);
                                    }
                                    offset += row_bytes;
                                }
                            }
                        }
                    }

                    b++;
                }
            }
        }
    }

    return (int)nbricks;
}

/* ====================================================================
 *  INTERNAL: image_fill_nifti1_from_header
 *
 *  Populate a nifti_image from a NIfTI-1 on-disk header.
 * ==================================================================== */
static int image_fill_nifti1_from_header(nifti_image *nim,
                                         const nifti_1_header *hdr)
{
    if (!nim || !hdr) return -1;

    nim->ndim = hdr->dim[0];
    if (nim->ndim < 1 || nim->ndim > 7) nim->ndim = 3;

    nim->nx = (nim->ndim >= 1) ? hdr->dim[1] : 1;
    nim->ny = (nim->ndim >= 2) ? hdr->dim[2] : 1;
    nim->nz = (nim->ndim >= 3) ? hdr->dim[3] : 1;
    nim->nt = (nim->ndim >= 4) ? hdr->dim[4] : 1;
    nim->nu = (nim->ndim >= 5) ? hdr->dim[5] : 1;
    nim->nv = (nim->ndim >= 6) ? hdr->dim[6] : 1;
    nim->nw = (nim->ndim >= 7) ? hdr->dim[7] : 1;

    if (nim->nx <= 0) nim->nx = 1;
    if (nim->ny <= 0) nim->ny = 1;
    if (nim->nz <= 0) nim->nz = 1;
    if (nim->nt <= 0) nim->nt = 1;
    if (nim->nu <= 0) nim->nu = 1;
    if (nim->nv <= 0) nim->nv = 1;
    if (nim->nw <= 0) nim->nw = 1;

    nim->nvox = nim->nx;
    if (nim->ndim >= 2) nim->nvox *= nim->ny;
    if (nim->ndim >= 3) nim->nvox *= nim->nz;
    if (nim->ndim >= 4) nim->nvox *= nim->nt;
    if (nim->ndim >= 5) nim->nvox *= nim->nu;
    if (nim->ndim >= 6) nim->nvox *= nim->nv;
    if (nim->ndim >= 7) nim->nvox *= nim->nw;

    nim->datatype = hdr->datatype;
    nim->nbyper   = nifti_datatype_bytes(hdr->datatype);
    nim->swapsize = nifti_datatype_swapsize(hdr->datatype);

    /* pixdim */
    nim->dx = (nim->ndim >= 1) ? (double)hdr->pixdim[1] : 1.0;
    nim->dy = (nim->ndim >= 2) ? (double)hdr->pixdim[2] : 1.0;
    nim->dz = (nim->ndim >= 3) ? (double)hdr->pixdim[3] : 1.0;
    nim->dt = (nim->ndim >= 4) ? (double)hdr->pixdim[4] : 1.0;
    nim->du = (nim->ndim >= 5) ? (double)hdr->pixdim[5] : 1.0;
    nim->dv = (nim->ndim >= 6) ? (double)hdr->pixdim[6] : 1.0;
    nim->dw = (nim->ndim >= 7) ? (double)hdr->pixdim[7] : 1.0;

    /* fix zero pixdims */
    if (nim->dx <= 0.0) nim->dx = 1.0;
    if (nim->dy <= 0.0) nim->dy = 1.0;
    if (nim->dz <= 0.0) nim->dz = 1.0;
    if (nim->dt <= 0.0) nim->dt = 1.0;
    if (nim->du <= 0.0) nim->du = 1.0;
    if (nim->dv <= 0.0) nim->dv = 1.0;
    if (nim->dw <= 0.0) nim->dw = 1.0;

    nim->scl_slope = (hdr->scl_slope != 0.0f) ? (double)hdr->scl_slope : 1.0;
    nim->scl_inter = (double)hdr->scl_inter;
    nim->cal_min   = (double)hdr->cal_min;
    nim->cal_max   = (double)hdr->cal_max;

    nim->qform_code = hdr->qform_code;
    nim->sform_code = hdr->sform_code;

    /* dim_info: freq_dim, phase_dim, slice_dim */
    nim->freq_dim  = (int)(hdr->dim_info & 0x03);
    nim->phase_dim = (int)((hdr->dim_info >> 2) & 0x03);
    nim->slice_dim = (int)((hdr->dim_info >> 4) & 0x03);

    nim->slice_code    = hdr->slice_code;
    nim->slice_start   = hdr->slice_start;
    nim->slice_end     = hdr->slice_end;
    nim->slice_duration= (double)hdr->slice_duration;

    nim->quatern_b = (double)hdr->quatern_b;
    nim->quatern_c = (double)hdr->quatern_c;
    nim->quatern_d = (double)hdr->quatern_d;
    nim->qoffset_x = (double)hdr->qoffset_x;
    nim->qoffset_y = (double)hdr->qoffset_y;
    nim->qoffset_z = (double)hdr->qoffset_z;
    nim->qfac      = (double)hdr->pixdim[0];

    /* srow → sto_xyz */
    {
        int r;
        for (r = 0; r < 4; r++) {
            nim->sto_xyz.m[0][r] = (double)hdr->srow_x[r];
            nim->sto_xyz.m[1][r] = (double)hdr->srow_y[r];
            nim->sto_xyz.m[2][r] = (double)hdr->srow_z[r];
        }
        /* ensure bottom row is identity-like */
        nim->sto_xyz.m[3][0] = 0.0;
        nim->sto_xyz.m[3][1] = 0.0;
        nim->sto_xyz.m[3][2] = 0.0;
        nim->sto_xyz.m[3][3] = 1.0;
    }

    nim->toffset   = (double)hdr->toffset;
    nim->xyz_units = (int)(hdr->xyzt_units & 0x07);
    nim->time_units= (int)((hdr->xyzt_units >> 3) & 0x07);

    /* nifti_type from magic */
    if (strncmp(hdr->magic, "n+1", 3) == 0) {
        nim->nifti_type = NIFTI_FTYPE_NIFTI1_2;
    } else if (strncmp(hdr->magic, "ni1", 3) == 0) {
        nim->nifti_type = NIFTI_FTYPE_NIFTI1_1;
    } else {
        nim->nifti_type = NIFTI_FTYPE_NIFTI1_1; /* default */
    }

    nim->intent_code = hdr->intent_code;
    nim->intent_p1   = (double)hdr->intent_p1;
    nim->intent_p2   = (double)hdr->intent_p2;
    nim->intent_p3   = (double)hdr->intent_p3;

    /* iname_offset for .nii = vox_offset; for .hdr/.img = 0 */
    nim->iname_offset = (int64_t)hdr->vox_offset;

    return 0;
}

/* ====================================================================
 *  INTERNAL: image_fill_nifti2_from_header
 *
 *  Populate a nifti_image from a NIfTI-2 on-disk header.
 * ==================================================================== */
static int image_fill_nifti2_from_header(nifti_image *nim,
                                         const nifti_2_header *hdr)
{
    if (!nim || !hdr) return -1;

    nim->ndim = (int64_t)hdr->dim[0];
    if (nim->ndim < 1 || nim->ndim > 7) nim->ndim = 3;

    nim->nx = (nim->ndim >= 1) ? hdr->dim[1] : 1;
    nim->ny = (nim->ndim >= 2) ? hdr->dim[2] : 1;
    nim->nz = (nim->ndim >= 3) ? hdr->dim[3] : 1;
    nim->nt = (nim->ndim >= 4) ? hdr->dim[4] : 1;
    nim->nu = (nim->ndim >= 5) ? hdr->dim[5] : 1;
    nim->nv = (nim->ndim >= 6) ? hdr->dim[6] : 1;
    nim->nw = (nim->ndim >= 7) ? hdr->dim[7] : 1;

    if (nim->nx <= 0) nim->nx = 1;
    if (nim->ny <= 0) nim->ny = 1;
    if (nim->nz <= 0) nim->nz = 1;
    if (nim->nt <= 0) nim->nt = 1;
    if (nim->nu <= 0) nim->nu = 1;
    if (nim->nv <= 0) nim->nv = 1;
    if (nim->nw <= 0) nim->nw = 1;

    nim->nvox = nim->nx;
    if (nim->ndim >= 2) nim->nvox *= nim->ny;
    if (nim->ndim >= 3) nim->nvox *= nim->nz;
    if (nim->ndim >= 4) nim->nvox *= nim->nt;
    if (nim->ndim >= 5) nim->nvox *= nim->nu;
    if (nim->ndim >= 6) nim->nvox *= nim->nv;
    if (nim->ndim >= 7) nim->nvox *= nim->nw;

    nim->datatype = hdr->datatype;
    nim->nbyper   = nifti_datatype_bytes(hdr->datatype);
    nim->swapsize = nifti_datatype_swapsize(hdr->datatype);

    nim->dx = (nim->ndim >= 1) ? hdr->pixdim[1] : 1.0;
    nim->dy = (nim->ndim >= 2) ? hdr->pixdim[2] : 1.0;
    nim->dz = (nim->ndim >= 3) ? hdr->pixdim[3] : 1.0;
    nim->dt = (nim->ndim >= 4) ? hdr->pixdim[4] : 1.0;
    nim->du = (nim->ndim >= 5) ? hdr->pixdim[5] : 1.0;
    nim->dv = (nim->ndim >= 6) ? hdr->pixdim[6] : 1.0;
    nim->dw = (nim->ndim >= 7) ? hdr->pixdim[7] : 1.0;

    if (nim->dx <= 0.0) nim->dx = 1.0;
    if (nim->dy <= 0.0) nim->dy = 1.0;
    if (nim->dz <= 0.0) nim->dz = 1.0;
    if (nim->dt <= 0.0) nim->dt = 1.0;
    if (nim->du <= 0.0) nim->du = 1.0;
    if (nim->dv <= 0.0) nim->dv = 1.0;
    if (nim->dw <= 0.0) nim->dw = 1.0;

    nim->scl_slope = (hdr->scl_slope != 0.0) ? hdr->scl_slope : 1.0;
    nim->scl_inter = hdr->scl_inter;
    nim->cal_min   = hdr->cal_min;
    nim->cal_max   = hdr->cal_max;

    nim->qform_code = hdr->qform_code;
    nim->sform_code = hdr->sform_code;

    nim->freq_dim  = (int)(hdr->dim_info & 0x03);
    nim->phase_dim = (int)((hdr->dim_info >> 2) & 0x03);
    nim->slice_dim = (int)((hdr->dim_info >> 4) & 0x03);

    nim->slice_code     = hdr->slice_code;
    nim->slice_start    = hdr->slice_start;
    nim->slice_end      = hdr->slice_end;
    nim->slice_duration = hdr->slice_duration;

    nim->quatern_b = hdr->quatern_b;
    nim->quatern_c = hdr->quatern_c;
    nim->quatern_d = hdr->quatern_d;
    nim->qoffset_x = hdr->qoffset_x;
    nim->qoffset_y = hdr->qoffset_y;
    nim->qoffset_z = hdr->qoffset_z;
    nim->qfac      = hdr->pixdim[0];

    {
        int r;
        for (r = 0; r < 4; r++) {
            nim->sto_xyz.m[0][r] = hdr->srow_x[r];
            nim->sto_xyz.m[1][r] = hdr->srow_y[r];
            nim->sto_xyz.m[2][r] = hdr->srow_z[r];
        }
        nim->sto_xyz.m[3][0] = 0.0;
        nim->sto_xyz.m[3][1] = 0.0;
        nim->sto_xyz.m[3][2] = 0.0;
        nim->sto_xyz.m[3][3] = 1.0;
    }

    nim->toffset   = hdr->toffset;
    nim->xyz_units = (int)(hdr->xyzt_units & 0x07);
    nim->time_units= (int)((hdr->xyzt_units >> 3) & 0x07);

    if (strncmp(hdr->magic, "n+2", 3) == 0) {
        nim->nifti_type = NIFTI_FTYPE_NIFTI2_2;
    } else {
        nim->nifti_type = NIFTI_FTYPE_NIFTI2_1;
    }

    nim->intent_code = hdr->intent_code;
    nim->intent_p1   = hdr->intent_p1;
    nim->intent_p2   = hdr->intent_p2;
    nim->intent_p3   = hdr->intent_p3;

    nim->iname_offset = hdr->vox_offset;

    return 0;
}

/* ====================================================================
 *  INTERNAL: image_build_nifti1_header
 *
 *  Reverse of image_fill_nifti1_from_header: populate a nifti_1_header
 *  from a nifti_image.  Caller provides the struct.
 * ==================================================================== */
static void image_build_nifti1_header(const nifti_image *nim,
                                      nifti_1_header *hdr)
{
    int64_t ndim;
    int c;

    if (!nim || !hdr) return;

    memset(hdr, 0, sizeof(nifti_1_header));

    hdr->sizeof_hdr = 348;

    ndim = nim->ndim;
    if (ndim < 1) ndim = 1;
    if (ndim > 7) ndim = 7;

    hdr->dim[0] = (short)ndim;
    hdr->dim[1] = (short)nim->nx;
    hdr->dim[2] = (short)nim->ny;
    hdr->dim[3] = (short)nim->nz;
    hdr->dim[4] = (short)nim->nt;
    hdr->dim[5] = (short)nim->nu;
    hdr->dim[6] = (short)nim->nv;
    hdr->dim[7] = (short)nim->nw;

    /* zero out unused dims */
    for (c = (int)ndim + 1; c <= 7; c++) {
        hdr->dim[c] = 0;
    }

    hdr->pixdim[0] = (float)nim->qfac;
    hdr->pixdim[1] = (float)nim->dx;
    hdr->pixdim[2] = (float)nim->dy;
    hdr->pixdim[3] = (float)nim->dz;
    hdr->pixdim[4] = (float)nim->dt;
    hdr->pixdim[5] = (float)nim->du;
    hdr->pixdim[6] = (float)nim->dv;
    hdr->pixdim[7] = (float)nim->dw;

    hdr->datatype   = (short)nim->datatype;
    hdr->bitpix     = (short)(nim->nbyper * 8);
    hdr->vox_offset = 352.0f;  /* default: after header + extender */
    hdr->scl_slope  = (float)nim->scl_slope;
    hdr->scl_inter  = (float)nim->scl_inter;
    hdr->cal_min    = (float)nim->cal_min;
    hdr->cal_max    = (float)nim->cal_max;

    hdr->qform_code = (short)nim->qform_code;
    hdr->sform_code = (short)nim->sform_code;

    hdr->dim_info   = (char)(
        (nim->freq_dim  & 0x03)       |
        ((nim->phase_dim & 0x03) << 2) |
        ((nim->slice_dim & 0x03) << 4));

    hdr->slice_code    = (char)nim->slice_code;
    hdr->slice_start   = (short)nim->slice_start;
    hdr->slice_end     = (short)nim->slice_end;
    hdr->slice_duration= (float)nim->slice_duration;

    hdr->quatern_b = (float)nim->quatern_b;
    hdr->quatern_c = (float)nim->quatern_c;
    hdr->quatern_d = (float)nim->quatern_d;
    hdr->qoffset_x = (float)nim->qoffset_x;
    hdr->qoffset_y = (float)nim->qoffset_y;
    hdr->qoffset_z = (float)nim->qoffset_z;

    /* srow from sto_xyz */
    {
        int r;
        for (r = 0; r < 4; r++) {
            hdr->srow_x[r] = (float)nim->sto_xyz.m[0][r];
            hdr->srow_y[r] = (float)nim->sto_xyz.m[1][r];
            hdr->srow_z[r] = (float)nim->sto_xyz.m[2][r];
        }
    }

    hdr->toffset    = (float)nim->toffset;
    hdr->xyzt_units = (char)((nim->xyz_units & 0x07) |
                             ((nim->time_units & 0x07) << 3));

    hdr->intent_code = (short)nim->intent_code;
    hdr->intent_p1   = (float)nim->intent_p1;
    hdr->intent_p2   = (float)nim->intent_p2;
    hdr->intent_p3   = (float)nim->intent_p3;

    /* magic */
    if (nim->nifti_type == NIFTI_FTYPE_NIFTI1_2) {
        memcpy(hdr->magic, "n+1\0", 4);
    } else {
        memcpy(hdr->magic, "ni1\0", 4);
    }

    /* extensions present? */
    if (nim->num_ext > 0 && nim->ext_list) {
        hdr->vox_offset = 352.0f;
        {
            int i;
            for (i = 0; i < nim->num_ext; i++) {
                hdr->vox_offset += (float)nim->ext_list[i].esize;
            }
        }
        /* align to 16 bytes */
        {
            int off = (int)hdr->vox_offset;
            if (off % 16) {
                off = ((off / 16) + 1) * 16;
            }
            hdr->vox_offset = (float)off;
        }
        memset(hdr->extender, 1, 4);  /* extensions follow */
    } else {
        hdr->vox_offset = 352.0f;
        memset(hdr->extender, 0, 4);
    }
}

/* ====================================================================
 *  INTERNAL: image_build_nifti2_header
 * ==================================================================== */
static void image_build_nifti2_header(const nifti_image *nim,
                                      nifti_2_header *hdr)
{
    int64_t ndim;
    int c;

    if (!nim || !hdr) return;

    memset(hdr, 0, sizeof(nifti_2_header));

    hdr->sizeof_hdr = 540;

    ndim = nim->ndim;
    if (ndim < 1) ndim = 1;
    if (ndim > 7) ndim = 7;

    hdr->dim[0] = ndim;
    hdr->dim[1] = nim->nx;
    hdr->dim[2] = nim->ny;
    hdr->dim[3] = nim->nz;
    hdr->dim[4] = nim->nt;
    hdr->dim[5] = nim->nu;
    hdr->dim[6] = nim->nv;
    hdr->dim[7] = nim->nw;

    for (c = (int)ndim + 1; c <= 7; c++) {
        hdr->dim[c] = 0;
    }

    hdr->pixdim[0] = nim->qfac;
    hdr->pixdim[1] = nim->dx;
    hdr->pixdim[2] = nim->dy;
    hdr->pixdim[3] = nim->dz;
    hdr->pixdim[4] = nim->dt;
    hdr->pixdim[5] = nim->du;
    hdr->pixdim[6] = nim->dv;
    hdr->pixdim[7] = nim->dw;

    hdr->datatype   = (int16_t)nim->datatype;
    hdr->bitpix     = (int16_t)(nim->nbyper * 8);
    hdr->vox_offset = 544;  /* default for NIfTI-2 */
    hdr->scl_slope  = nim->scl_slope;
    hdr->scl_inter  = nim->scl_inter;
    hdr->cal_min    = nim->cal_min;
    hdr->cal_max    = nim->cal_max;

    hdr->qform_code = (int32_t)nim->qform_code;
    hdr->sform_code = (int32_t)nim->sform_code;

    hdr->dim_info   = (char)(
        (nim->freq_dim  & 0x03)       |
        ((nim->phase_dim & 0x03) << 2) |
        ((nim->slice_dim & 0x03) << 4));

    hdr->slice_code     = (int32_t)nim->slice_code;
    hdr->slice_start    = nim->slice_start;
    hdr->slice_end      = nim->slice_end;
    hdr->slice_duration = nim->slice_duration;

    hdr->quatern_b = nim->quatern_b;
    hdr->quatern_c = nim->quatern_c;
    hdr->quatern_d = nim->quatern_d;
    hdr->qoffset_x = nim->qoffset_x;
    hdr->qoffset_y = nim->qoffset_y;
    hdr->qoffset_z = nim->qoffset_z;

    {
        int r;
        for (r = 0; r < 4; r++) {
            hdr->srow_x[r] = nim->sto_xyz.m[0][r];
            hdr->srow_y[r] = nim->sto_xyz.m[1][r];
            hdr->srow_z[r] = nim->sto_xyz.m[2][r];
        }
    }

    hdr->toffset    = nim->toffset;
    hdr->xyzt_units = (int32_t)((nim->xyz_units & 0x07) |
                                ((nim->time_units & 0x07) << 3));

    hdr->intent_code = (int32_t)nim->intent_code;
    hdr->intent_p1   = nim->intent_p1;
    hdr->intent_p2   = nim->intent_p2;
    hdr->intent_p3   = nim->intent_p3;

    if (nim->nifti_type == NIFTI_FTYPE_NIFTI2_2) {
        memcpy(hdr->magic, "n+2\0\r\n\032\n", 8);
    } else {
        memcpy(hdr->magic, "ni2\0\r\n\032\n", 8);
    }
}

/* ====================================================================
 *  INTERNAL: image_compute_matrices
 *
 *  Compute qto_xyz, qto_ijk from quaternion parameters, and
 *  sto_ijk from sto_xyz (its inverse).
 * ==================================================================== */
static void image_compute_matrices(nifti_image *nim)
{
    if (!nim) return;

    /* qto_xyz from quaternion parameters */
    nim->qto_xyz = nifti_quatern_to_dmat44(
        nim->quatern_b, nim->quatern_c, nim->quatern_d,
        nim->qoffset_x, nim->qoffset_y, nim->qoffset_z,
        nim->dx, nim->dy, nim->dz, nim->qfac);

    /* qto_ijk = inverse of qto_xyz */
    nim->qto_ijk = nifti_dmat44_inverse(nim->qto_xyz);

    /* sto_ijk = inverse of sto_xyz */
    nim->sto_ijk = nifti_dmat44_inverse(nim->sto_xyz);
}

/* ====================================================================
 *  INTERNAL: image_read_data
 *
 *  Read voxel data from a file into nim->data.  Applies byte swapping
 *  if nim->byteorder != native byte order.
 * ==================================================================== */
static int image_read_data(const char *fname, int64_t offset,
                           nifti_image *nim)
{
    znzFile fp;
    int     use_comp;
    size_t  data_bytes;
    size_t  nread;

    if (!fname || !nim) return -1;
    if (nim->nvox <= 0 || nim->nbyper <= 0) return 0;

    data_bytes = (size_t)nim->nvox * (size_t)nim->nbyper;

    nim->data = malloc(data_bytes);
    if (!nim->data) return -1;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) {
        free(nim->data);
        nim->data = NULL;
        return -1;
    }

    /* seek to data offset */
    if (offset > 0) {
        if (znzseek(fp, (long long)offset, SEEK_SET) < 0) {
            znzclose(&fp);
            free(nim->data);
            nim->data = NULL;
            return -1;
        }
    }

    nread = znzread(nim->data, data_bytes, 1, fp);
    znzclose(&fp);

    if (nread != 1) {
        /* partial read — still keep what we got */
    }

    /* byte-swap if needed */
    if (nim->byteorder != nifti_short_order() && nim->swapsize > 0) {
        nifti_swap_Nbytes((size_t)nim->nvox, nim->swapsize, nim->data);
    }

    return 0;
}

/* ====================================================================
 *  INTERNAL: image_write_single_file
 *
 *  Write a .nii file: header + extensions + data in one file.
 * ==================================================================== */
static int image_write_single_file(nifti_image *nim, const char *fname)
{
    znzFile fp;
    int     use_comp;
    int     use_v2 = 0;
    size_t  data_bytes;
    int     needs_swap;

    if (!nim || !fname) return -1;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;

    /* decide NIfTI version */
    if (nim->nifti_type == NIFTI_FTYPE_NIFTI2_1 ||
        nim->nifti_type == NIFTI_FTYPE_NIFTI2_2) {
        use_v2 = 1;
    } else if (nim->nifti_type == NIFTI_FTYPE_NIFTI1_1 ||
               nim->nifti_type == NIFTI_FTYPE_NIFTI1_2 ||
               nim->nifti_type == 0) {
        use_v2 = 0;
    } else {
        /* unknown — default to NIfTI-1 */
        use_v2 = 0;
    }

    fp = znzopen(fname, "wb", use_comp);
    if (!fp) return -1;

    needs_swap = (nim->byteorder != nifti_short_order()) ? 1 : 0;

    /* ---- write header ---- */
    if (use_v2) {
        nifti_2_header h2;
        image_build_nifti2_header(nim, &h2);

        if (nim->num_ext > 0 && nim->ext_list) {
            int i;
            h2.vox_offset = 544;
            for (i = 0; i < nim->num_ext; i++) {
                h2.vox_offset += nim->ext_list[i].esize;
            }
            /* 16-byte alignment */
            if (h2.vox_offset % 16) {
                h2.vox_offset = ((h2.vox_offset / 16) + 1) * 16;
            }
        }

        if (needs_swap) {
            nifti_swap_header_2(&h2);
        }
        if (znzwrite(&h2, sizeof(nifti_2_header), 1, fp) != 1) {
            znzclose(&fp);
            return -1;
        }
    } else {
        nifti_1_header h1;
        image_build_nifti1_header(nim, &h1);

        if (needs_swap) {
            nifti_swap_header_1(&h1);
        }
        if (znzwrite(&h1, sizeof(nifti_1_header), 1, fp) != 1) {
            znzclose(&fp);
            return -1;
        }
    }

    /* ---- write extensions ---- */
    if (nim->num_ext > 0 && nim->ext_list) {
        int i;
        /* extender (4 bytes) already written as part of the header */
        for (i = 0; i < nim->num_ext; i++) {
            int esize = nim->ext_list[i].esize;
            int ecode = nim->ext_list[i].ecode;

            if (needs_swap) {
                int eswap = esize;
                int ecswap = ecode;
                nifti_swap_4bytes(1, &eswap);
                nifti_swap_4bytes(1, &ecswap);
                znzwrite(&eswap, 4, 1, fp);
                znzwrite(&ecswap, 4, 1, fp);
            } else {
                znzwrite(&esize, 4, 1, fp);
                znzwrite(&ecode, 4, 1, fp);
            }

            if (esize > 8 && nim->ext_list[i].edata) {
                znzwrite(nim->ext_list[i].edata,
                         (size_t)(esize - 8), 1, fp);
            }
        }

        /* pad to 16-byte boundary */
        {
            long long cur = znztell(fp);
            if (cur >= 0) {
                int pad = (int)(((cur + 15) / 16) * 16 - cur);
                if (pad > 0 && pad < 16) {
                    char padbuf[16] = {0};
                    znzwrite(padbuf, (size_t)pad, 1, fp);
                }
            }
        }
    }

    /* ---- write data ---- */
    if (nim->data && nim->nvox > 0 && nim->nbyper > 0) {
        data_bytes = (size_t)nim->nvox * (size_t)nim->nbyper;

        if (needs_swap && nim->swapsize > 0) {
            /* copy, swap, write, free */
            void *swapped = malloc(data_bytes);
            if (!swapped) {
                znzclose(&fp);
                return -1;
            }
            memcpy(swapped, nim->data, data_bytes);
            nifti_swap_Nbytes((size_t)nim->nvox, nim->swapsize, swapped);
            znzwrite(swapped, data_bytes, 1, fp);
            free(swapped);
        } else {
            znzwrite(nim->data, data_bytes, 1, fp);
        }
    }

    znzclose(&fp);
    return 0;
}

/* ====================================================================
 *  INTERNAL: image_write_hdr_img_pair
 *
 *  Write header to .hdr file, data to .img file.
 * ==================================================================== */
static int image_write_hdr_img_pair(nifti_image *nim,
                                     const char *hdr_fname,
                                     const char *img_fname)
{
    int    use_v2 = 0;
    int    use_comp;

    if (!nim || !hdr_fname || !img_fname) return -1;

    use_comp = nifti_is_gzfile(hdr_fname) ? 1 : 0;

    if (nim->nifti_type == NIFTI_FTYPE_NIFTI2_1 ||
        nim->nifti_type == NIFTI_FTYPE_NIFTI2_2) {
        use_v2 = 1;
    }

    /* ---- write header ---- */
    {
        znzFile fp = znzopen(hdr_fname, "wb", use_comp);
        if (!fp) return -1;

        if (use_v2) {
            nifti_2_header h2;
            image_build_nifti2_header(nim, &h2);
            h2.vox_offset = 0;  /* .hdr/.img: data starts at 0 in .img */
            if (znzwrite(&h2, sizeof(nifti_2_header), 1, fp) != 1) {
                znzclose(&fp);
                return -1;
            }
        } else {
            nifti_1_header h1;
            image_build_nifti1_header(nim, &h1);
            h1.vox_offset = 0.0f;
            if (znzwrite(&h1, sizeof(nifti_1_header), 1, fp) != 1) {
                znzclose(&fp);
                return -1;
            }
        }
        znzclose(&fp);
    }

    /* ---- write data ---- */
    if (nim->data && nim->nvox > 0 && nim->nbyper > 0) {
        znzFile fp;
        size_t  data_bytes;
        int     needs_swap;

        use_comp   = nifti_is_gzfile(img_fname) ? 1 : 0;
        needs_swap = (nim->byteorder != nifti_short_order()) ? 1 : 0;
        data_bytes = (size_t)nim->nvox * (size_t)nim->nbyper;

        fp = znzopen(img_fname, "wb", use_comp);
        if (!fp) return -1;

        if (needs_swap && nim->swapsize > 0) {
            void *swapped = malloc(data_bytes);
            if (!swapped) {
                znzclose(&fp);
                return -1;
            }
            memcpy(swapped, nim->data, data_bytes);
            nifti_swap_Nbytes((size_t)nim->nvox, nim->swapsize, swapped);
            znzwrite(swapped, data_bytes, 1, fp);
            free(swapped);
        } else {
            znzwrite(nim->data, data_bytes, 1, fp);
        }
        znzclose(&fp);
    }

    return 0;
}

/* ====================================================================
 *  INTERNAL: image_read_voxel_as_double
 *
 *  Read a single voxel from a raw buffer, interpreting it as the
 *  given NIfTI datatype, and return its value as a double.
 * ==================================================================== */
static double image_read_voxel_as_double(const void *src, int dtype,
                                         int64_t index)
{
    const char *p = (const char *)src;
    int nbyper = nifti_datatype_bytes(dtype);

    if (!src || nbyper <= 0) return 0.0;

    p += index * nbyper;

    switch (dtype) {
        case NIFTI_DT_UINT8:
        case NIFTI_DT_BINARY:
            return (double)(*(const unsigned char *)p);
        case NIFTI_DT_INT8:
            return (double)(*(const signed char *)p);
        case NIFTI_DT_INT16:
            return (double)(*(const short *)p);
        case NIFTI_DT_UINT16:
            return (double)(*(const unsigned short *)p);
        case NIFTI_DT_INT32:
            return (double)(*(const int *)p);
        case NIFTI_DT_UINT32:
            return (double)(*(const unsigned int *)p);
        case NIFTI_DT_INT64:
            return (double)(*(const int64_t *)p);
        case NIFTI_DT_UINT64:
            return (double)(*(const uint64_t *)p);
        case NIFTI_DT_FLOAT32:
            return (double)(*(const float *)p);
        case NIFTI_DT_FLOAT64:
            return *(const double *)p;
        case NIFTI_DT_FLOAT128:
            return (double)(*(const long double *)p);
        case NIFTI_DT_COMPLEX64:
            return (double)((const nifti_complex_float *)p)->r;
        case NIFTI_DT_COMPLEX128:
            return ((const nifti_complex_double *)p)->r;
        case NIFTI_DT_COMPLEX256:
            return (double)((const nifti_complex_longdouble *)p)->r;
        case NIFTI_DT_RGB24:
            /* average of R,G,B as a heuristic */
            return ((double)((const nifti_rgb_byte *)p)->r +
                    (double)((const nifti_rgb_byte *)p)->g +
                    (double)((const nifti_rgb_byte *)p)->b) / 3.0;
        case NIFTI_DT_RGBA32:
            return ((double)((const nifti_rgba_byte *)p)->r +
                    (double)((const nifti_rgba_byte *)p)->g +
                    (double)((const nifti_rgba_byte *)p)->b) / 3.0;
        default:
            return 0.0;
    }
}

/* ====================================================================
 *  INTERNAL: image_write_voxel_from_double
 *
 *  Write a double into a raw buffer at the given linear index,
 *  converting to the specified NIfTI datatype.
 * ==================================================================== */
static void image_write_voxel_from_double(void *dst, int dtype,
                                           int64_t index, double val)
{
    char *p = (char *)dst;
    int nbyper = nifti_datatype_bytes(dtype);

    if (!dst || nbyper <= 0) return;

    p += index * nbyper;

    switch (dtype) {
        case NIFTI_DT_UINT8:
        case NIFTI_DT_BINARY:
            *(unsigned char *)p = (unsigned char)(val + 0.5);
            break;
        case NIFTI_DT_INT8:
            *(signed char *)p = (signed char)(val + 0.5);
            break;
        case NIFTI_DT_INT16:
            *(short *)p = (short)(val + 0.5);
            break;
        case NIFTI_DT_UINT16:
            *(unsigned short *)p = (unsigned short)(val + 0.5);
            break;
        case NIFTI_DT_INT32:
            *(int *)p = (int)(val + 0.5);
            break;
        case NIFTI_DT_UINT32:
            *(unsigned int *)p = (unsigned int)(val + 0.5);
            break;
        case NIFTI_DT_INT64:
            *(int64_t *)p = (int64_t)(val + 0.5);
            break;
        case NIFTI_DT_UINT64:
            *(uint64_t *)p = (uint64_t)(val + 0.5);
            break;
        case NIFTI_DT_FLOAT32:
            *(float *)p = (float)val;
            break;
        case NIFTI_DT_FLOAT64:
            *(double *)p = val;
            break;
        case NIFTI_DT_FLOAT128:
            *(long double *)p = (long double)val;
            break;
        case NIFTI_DT_COMPLEX64:
            ((nifti_complex_float *)p)->r = (float)val;
            ((nifti_complex_float *)p)->i = 0.0f;
            break;
        case NIFTI_DT_COMPLEX128:
            ((nifti_complex_double *)p)->r = val;
            ((nifti_complex_double *)p)->i = 0.0;
            break;
        case NIFTI_DT_COMPLEX256:
            ((nifti_complex_longdouble *)p)->r = (long double)val;
            ((nifti_complex_longdouble *)p)->i = 0.0L;
            break;
        case NIFTI_DT_RGB24:
            {
                unsigned char v = (unsigned char)(val + 0.5);
                ((nifti_rgb_byte *)p)->r = v;
                ((nifti_rgb_byte *)p)->g = v;
                ((nifti_rgb_byte *)p)->b = v;
            }
            break;
        case NIFTI_DT_RGBA32:
            {
                unsigned char v = (unsigned char)(val + 0.5);
                ((nifti_rgba_byte *)p)->r = v;
                ((nifti_rgba_byte *)p)->g = v;
                ((nifti_rgba_byte *)p)->b = v;
                ((nifti_rgba_byte *)p)->a = 255;
            }
            break;
        default:
            break;
    }
}

/* ====================================================================
 *  INTERNAL: image_clamp_double_for_dtype
 *
 *  Clamp a double value to the representable range of a NIfTI datatype.
 *  Rounds to nearest integer for integer types.
 * ==================================================================== */
static void image_clamp_double_for_dtype(double *val, int dtype)
{
    if (!val) return;

    switch (dtype) {
        case NIFTI_DT_UINT8:
        case NIFTI_DT_BINARY:
            if (*val < 0.0) *val = 0.0;
            if (*val > 255.0) *val = 255.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_INT8:
            if (*val < -128.0) *val = -128.0;
            if (*val > 127.0) *val = 127.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_INT16:
            if (*val < -32768.0) *val = -32768.0;
            if (*val > 32767.0) *val = 32767.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_UINT16:
            if (*val < 0.0) *val = 0.0;
            if (*val > 65535.0) *val = 65535.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_INT32:
            if (*val < -2147483648.0) *val = -2147483648.0;
            if (*val > 2147483647.0) *val = 2147483647.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_UINT32:
            if (*val < 0.0) *val = 0.0;
            if (*val > 4294967295.0) *val = 4294967295.0;
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_INT64:
            /* can't perfectly represent full int64 in double, but close */
            *val = floor(*val + 0.5);
            break;
        case NIFTI_DT_UINT64:
            if (*val < 0.0) *val = 0.0;
            *val = floor(*val + 0.5);
            break;
        default:
            /* float/complex/RGB types: no clamping needed */
            break;
    }
}

#endif /* NIFTI_IMAGE_IMPLEMENTED */
#endif /* NIFTI_IMAGE_IMPLEMENTATION */
