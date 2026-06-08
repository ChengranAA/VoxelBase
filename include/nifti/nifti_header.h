/*
   NIFTI_HEADER.H -- Layer 3: NIfTI-1 / NIfTI-2 header structures and I/O

   This is a header-only library (STB-style). To use it:

     #define NIFTI_HEADER_IMPLEMENTATION
     #include "nifti_header.h"

   Depends on Layer 1 (nifti_base.h) and Layer 2 (nifti_znz.h), which are
   included automatically.

   Provides:
     - nifti_1_header  (348 bytes, NIfTI-1 on-disk layout)
     - nifti_2_header  (540 bytes, NIfTI-2 on-disk layout)
     - nifti1_extension (extension list element)
     - Header read/write/validate/swap/display functions
     - Extension read/write/free/add functions

   PUBLIC DOMAIN - No warranty, use at your own risk.
*/

#ifndef NIFTI_HEADER_H
#define NIFTI_HEADER_H

#include "nifti_base.h"
#include "nifti_znz.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 *  Extension codes
 * ==================================================================== */
#define NIFTI_ECODE_IGNORE                 0
#define NIFTI_ECODE_DICOM                  2
#define NIFTI_ECODE_AFNI                   4
#define NIFTI_ECODE_COMMENT                6
#define NIFTI_ECODE_XCEDE                  8
#define NIFTI_ECODE_JIMDIMINFO            10
#define NIFTI_ECODE_WORKFLOW_FWDS         12
#define NIFTI_ECODE_FREESURFER            14
#define NIFTI_ECODE_PYPICKLE              16
#define NIFTI_ECODE_MIND_IDENT            18
#define NIFTI_ECODE_B_VALUE               20
#define NIFTI_ECODE_SPHERICAL_DIRECTION   22
#define NIFTI_ECODE_DT_COMPONENT          24
#define NIFTI_ECODE_SHC_DEGREEORDER       26
#define NIFTI_ECODE_VOXBO                 28
#define NIFTI_ECODE_CARET                 30
#define NIFTI_ECODE_CIFTI                 32
#define NIFTI_ECODE_VARIABLE_FRAME_TIMING 34
#define NIFTI_ECODE_EVAL                  36
#define NIFTI_ECODE_MATLAB                40
#define NIFTI_ECODE_QUANTIPHYSE           44
#define NIFTI_ECODE_MRS                   48

/* ====================================================================
 *  NIfTI-1 header structure  (exactly 348 bytes, packed)
 * ==================================================================== */
#pragma pack(1)
typedef struct {
    int    sizeof_hdr;      /* must be 348                                     */
    char   data_type[10];   /* unused                                          */
    char   db_name[18];     /* unused                                          */
    int    extents;         /* unused                                          */
    short  session_error;   /* unused                                          */
    char   regular;         /* unused                                          */
    char   dim_info;        /* MRI slice ordering                              */
    short  dim[8];          /* dimensions (dim[0] = ndim, dim[1..7] = sizes)   */
    float  intent_p1;       /* 1st intent parameter                           */
    float  intent_p2;       /* 2nd intent parameter                           */
    float  intent_p3;       /* 3rd intent parameter                           */
    short  intent_code;     /* NIFTI_INTENT_* code                             */
    short  datatype;        /* NIFTI_DT_* code                                 */
    short  bitpix;          /* bits per pixel                                  */
    short  slice_start;     /* first slice index                               */
    float  pixdim[8];       /* grid spacings (pixdim[0] = qfac)                */
    float  vox_offset;      /* byte offset from header start to data           */
    float  scl_slope;       /* scaling slope                                   */
    float  scl_inter;       /* scaling intercept                               */
    short  slice_end;       /* last slice index                                */
    char   slice_code;      /* NIFTI_SLICE_* code                              */
    char   xyzt_units;      /* spatial + temporal units                        */
    float  cal_max;         /* calibrated max display value                    */
    float  cal_min;         /* calibrated min display value                    */
    float  slice_duration;  /* time for one slice                              */
    float  toffset;         /* time offset                                     */
    int    glmax;           /* unused (global max)                             */
    int    glmin;           /* unused (global min)                             */
    char   descrip[80];     /* description                                     */
    char   aux_file[24];    /* auxiliary filename                              */
    short  qform_code;      /* NIFTI_XFORM_* code for quaternion transform     */
    short  sform_code;      /* NIFTI_XFORM_* code for affine transform         */
    float  quatern_b;       /* quaternion b param                              */
    float  quatern_c;       /* quaternion c param                              */
    float  quatern_d;       /* quaternion d param                              */
    float  qoffset_x;       /* quaternion x shift                             */
    float  qoffset_y;       /* quaternion y shift                             */
    float  qoffset_z;       /* quaternion z shift                             */
    float  srow_x[4];       /* affine transform row 1                          */
    float  srow_y[4];       /* affine transform row 2                          */
    float  srow_z[4];       /* affine transform row 3                          */
    char   intent_name[16]; /* name or meaning of the data                     */
    char   magic[4];        /* "ni1\0" or "n+1\0"                              */
    char   extender[4];     /* extension indicator (must be 0,0,0,0 if none)   */
} nifti_1_header;
#pragma pack()

/* ====================================================================
 *  NIfTI-2 header structure  (exactly 540 bytes, packed)
 * ==================================================================== */
#pragma pack(1)
typedef struct {
    int32_t sizeof_hdr;     /* must be 540                                     */
    char    magic[8];       /* "n+2\0\r\n\032\n" or "ni2\0\r\n\032\n"          */
    int16_t datatype;       /* NIFTI_DT_* code                                 */
    int16_t bitpix;         /* bits per pixel                                  */
    int64_t dim[8];         /* dimensions (dim[0] = ndim, dim[1..7] = sizes)   */
    double  intent_p1;      /* 1st intent parameter                           */
    double  intent_p2;      /* 2nd intent parameter                           */
    double  intent_p3;      /* 3rd intent parameter                           */
    double  pixdim[8];      /* grid spacings (pixdim[0] = qfac)                */
    int64_t vox_offset;     /* byte offset from header start to data           */
    double  scl_slope;      /* scaling slope                                   */
    double  scl_inter;      /* scaling intercept                               */
    double  cal_max;        /* calibrated max display value                    */
    double  cal_min;        /* calibrated min display value                    */
    double  slice_duration; /* time for one slice                              */
    double  toffset;        /* time offset                                     */
    int64_t slice_start;    /* first slice index                               */
    int64_t slice_end;      /* last slice index                                */
    char    descrip[80];    /* description                                     */
    char    aux_file[24];   /* auxiliary filename                              */
    int32_t qform_code;     /* NIFTI_XFORM_* code for quaternion               */
    int32_t sform_code;     /* NIFTI_XFORM_* code for affine                   */
    double  quatern_b;      /* quaternion b param                              */
    double  quatern_c;      /* quaternion c param                              */
    double  quatern_d;      /* quaternion d param                              */
    double  qoffset_x;      /* quaternion x shift                             */
    double  qoffset_y;      /* quaternion y shift                             */
    double  qoffset_z;      /* quaternion z shift                             */
    double  srow_x[4];      /* affine transform row 1                          */
    double  srow_y[4];      /* affine transform row 2                          */
    double  srow_z[4];      /* affine transform row 3                          */
    int32_t slice_code;     /* NIFTI_SLICE_* code                              */
    int32_t xyzt_units;     /* spatial + temporal units                        */
    int32_t intent_code;    /* NIFTI_INTENT_* code                             */
    char    intent_name[16];/* name or meaning of the data                     */
    char    dim_info;       /* MRI slice ordering                              */
    char    unused_str[15]; /* padding to reach 540 bytes                      */
} nifti_2_header;
#pragma pack()

/* ====================================================================
 *  Extension structure
 * ==================================================================== */
typedef struct {
    int   esize;   /* total size of extension, including esize and ecode (8)   */
    int   ecode;   /* extension code (one of NIFTI_ECODE_*)                    */
    char *edata;   /* extension data (esize - 8 bytes)                         */
} nifti1_extension;

/* ====================================================================
 *  Declared API functions
 * ==================================================================== */

/* Check file magic without reading full header */
int  nifti_is_nifti1_file(const char *fname);
int  nifti_is_nifti2_file(const char *fname);

/* Validate a header in memory */
int  nifti_hdr1_looks_good(const nifti_1_header *hdr);
int  nifti_hdr2_looks_good(const nifti_2_header *hdr);

/* Check whether bytes need swapping (0=no swap, 1=swap, -1=unknown) */
int  nifti_needs_swap_1(const nifti_1_header *hdr);
int  nifti_needs_swap_2(const nifti_2_header *hdr);

/* Byte-swap a header in place */
void nifti_swap_header_1(nifti_1_header *hdr);
void nifti_swap_header_2(nifti_2_header *hdr);

/* Read a header from a file (supports .gz via znz).
   Returns a malloc'd header that the caller must free(), or NULL on error.
   If needs_swap is not NULL, it is set to 0 or 1. */
nifti_1_header *nifti_read_header_1(const char *fname, int *needs_swap);
nifti_2_header *nifti_read_header_2(const char *fname, int *needs_swap);

/* Write a header to a file (supports .gz via znz).
   Returns 0 on success, non-zero on error. */
int  nifti_write_header_1(const char *fname, const nifti_1_header *hdr);
int  nifti_write_header_2(const char *fname, const nifti_2_header *hdr);

/* Read extensions from a .nii file.  Must be called after the header is read;
   extensions begin at byte 352 (NIfTI-1) or byte 544 (NIfTI-2) and end
   before vox_offset.  num_ext is updated with the count found.
   Returns 0 on success, non-zero on error. */
int  nifti_read_extensions(const char *fname, int *num_ext,
                           nifti1_extension **ext_list);

/* Write extensions to a .nii file.
   Returns 0 on success, non-zero on error. */
int  nifti_write_extensions(const char *fname, int num_ext,
                            const nifti1_extension *ext_list);

/* Free an extension list allocated by nifti_read_extensions or
   nifti_add_extension. */
void nifti_free_extensions(int num_ext, nifti1_extension *ext_list);

/* Add an extension to the list (reallocs ext_list, updates *num_ext).
   esize is the size of edata in bytes.  Returns 0 on success. */
int  nifti_add_extension(int ecode, const char *edata, int esize,
                         int *num_ext, nifti1_extension **ext_list);

/* Print header fields to stdout (human-readable). */
void nifti_disp_header_1(const nifti_1_header *hdr);
void nifti_disp_header_2(const nifti_2_header *hdr);

#ifdef __cplusplus
}
#endif

#endif /* NIFTI_HEADER_H */

/* ====================================================================
 *  IMPLEMENTATION
 * ==================================================================== */
#ifdef NIFTI_HEADER_IMPLEMENTATION
#ifndef NIFTI_HEADER_IMPLEMENTED
#define NIFTI_HEADER_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 *  Internal helpers
 * ------------------------------------------------------------------ */

/* Byte-swap in place: 2 bytes */
static void nifti_header_swap_2(void *p, size_t n) {
    size_t i;
    unsigned char *cp = (unsigned char *)p;
    for (i = 0; i < n; i++, cp += 2) {
        unsigned char t  = cp[0];
        cp[0] = cp[1];
        cp[1] = t;
    }
}

/* Byte-swap in place: 4 bytes */
static void nifti_header_swap_4(void *p, size_t n) {
    size_t i;
    unsigned char *cp = (unsigned char *)p;
    unsigned char t;
    for (i = 0; i < n; i++, cp += 4) {
        t    = cp[0];
        cp[0] = cp[3];
        cp[3] = t;
        t    = cp[1];
        cp[1] = cp[2];
        cp[2] = t;
    }
}

/* Byte-swap in place: 8 bytes */
static void nifti_header_swap_8(void *p, size_t n) {
    size_t i;
    unsigned char *cp = (unsigned char *)p;
    unsigned char t;
    for (i = 0; i < n; i++, cp += 8) {
        t    = cp[0];
        cp[0] = cp[7];
        cp[7] = t;
        t    = cp[1];
        cp[1] = cp[6];
        cp[6] = t;
        t    = cp[2];
        cp[2] = cp[5];
        cp[5] = t;
        t    = cp[3];
        cp[3] = cp[4];
        cp[4] = t;
    }
}

/* ------------------------------------------------------------------
 *  nifti_is_nifti1_file
 * ------------------------------------------------------------------ */
int nifti_is_nifti1_file(const char *fname) {
    znzFile fp;
    char magic[4];
    int use_comp;

    if (!fname) return 0;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) return 0;

    /* magic is at offset 344 from beginning */
    if (znzseek(fp, 344, SEEK_SET) < 0) {
        znzclose(&fp);
        return 0;
    }

    if (znzread(magic, 1, 4, fp) != 4) {
        znzclose(&fp);
        return 0;
    }

    znzclose(&fp);

    return (strncmp(magic, "ni1", 3) == 0 || strncmp(magic, "n+1", 3) == 0)
           && magic[3] == '\0';
}

/* ------------------------------------------------------------------
 *  nifti_is_nifti2_file
 * ------------------------------------------------------------------ */
int nifti_is_nifti2_file(const char *fname) {
    znzFile fp;
    int32_t sizeof_hdr;
    char magic[8];
    int use_comp;

    if (!fname) return 0;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) return 0;

    /* NIfTI-2: first 4 bytes = sizeof_hdr (should be 540), then 8 bytes magic */
    if (znzread(&sizeof_hdr, 4, 1, fp) != 1) {
        znzclose(&fp);
        return 0;
    }

    if (znzread(magic, 1, 8, fp) != 8) {
        znzclose(&fp);
        return 0;
    }

    znzclose(&fp);

    /* check if sizeof_hdr is 540 (native) or byte-swapped */
    if (sizeof_hdr != 540 && sizeof_hdr != 469893120) return 0;

    return (strncmp(magic, "n+2", 3) == 0 || strncmp(magic, "ni2", 3) == 0);
}

/* ------------------------------------------------------------------
 *  nifti_needs_swap_1
 * ------------------------------------------------------------------ */
int nifti_needs_swap_1(const nifti_1_header *hdr) {
    if (!hdr) return -1;
    return (hdr->sizeof_hdr == 348) ? 0 :
           ((hdr->sizeof_hdr == 1543569408) ? 1 : -1);
}

/* ------------------------------------------------------------------
 *  nifti_needs_swap_2
 * ------------------------------------------------------------------ */
int nifti_needs_swap_2(const nifti_2_header *hdr) {
    if (!hdr) return -1;
    return (hdr->sizeof_hdr == 540) ? 0 :
           ((hdr->sizeof_hdr == 469893120) ? 1 : -1);
}

/* ------------------------------------------------------------------
 *  nifti_swap_header_1
 * ------------------------------------------------------------------ */
void nifti_swap_header_1(nifti_1_header *hdr) {
    if (!hdr) return;

    /* sizeof_hdr, extents, glmax, glmin  -- each 4 bytes, 4 fields */
    nifti_header_swap_4(&hdr->sizeof_hdr, 4);

    /* session_error, dim[8], intent_code, datatype, bitpix,
       slice_start, slice_end, qform_code, sform_code  -- each 2 bytes */
    {
        short *s = (short *)hdr->dim;
        nifti_header_swap_2(&hdr->session_error, 1);   /* session_error */
        nifti_header_swap_2(s, 8);                      /* dim[8]       */
        nifti_header_swap_2(&hdr->intent_code, 1);      /* intent_code  */
        nifti_header_swap_2(&hdr->datatype, 1);         /* datatype     */
        nifti_header_swap_2(&hdr->bitpix, 1);           /* bitpix       */
        nifti_header_swap_2(&hdr->slice_start, 1);      /* slice_start  */
        nifti_header_swap_2(&hdr->slice_end, 1);        /* slice_end    */
        nifti_header_swap_2(&hdr->qform_code, 1);       /* qform_code   */
        nifti_header_swap_2(&hdr->sform_code, 1);       /* sform_code   */
    }

    /* intent_p1..p3, pixdim[8], vox_offset, scl_slope, scl_inter,
       cal_max, cal_min, slice_duration, toffset, quatern_b..d,
       qoffset_x..z, srow_x[4], srow_y[4], srow_z[4] -- each 4 bytes */
    nifti_header_swap_4(&hdr->intent_p1, 3);             /* intent_p1..3 */
    nifti_header_swap_4(hdr->pixdim, 8);                 /* pixdim[8]    */
    nifti_header_swap_4(&hdr->vox_offset, 1);            /* vox_offset   */
    nifti_header_swap_4(&hdr->scl_slope, 2);             /* scl_slope + scl_inter */
    nifti_header_swap_4(&hdr->cal_max, 2);               /* cal_max + cal_min     */
    nifti_header_swap_4(&hdr->slice_duration, 1);        /* slice_duration        */
    nifti_header_swap_4(&hdr->toffset, 1);               /* toffset               */
    nifti_header_swap_4(&hdr->glmax, 2);                 /* glmax + glmin         */
    nifti_header_swap_4(&hdr->quatern_b, 3);             /* quatern_b..d          */
    nifti_header_swap_4(&hdr->qoffset_x, 3);             /* qoffset_x..z          */
    nifti_header_swap_4(hdr->srow_x, 12);                /* srow_x/y/z[4]         */
}

/* ------------------------------------------------------------------
 *  nifti_swap_header_2
 * ------------------------------------------------------------------ */
void nifti_swap_header_2(nifti_2_header *hdr) {
    if (!hdr) return;

    /* sizeof_hdr -- 4 bytes, at offset 0 */
    nifti_header_swap_4(&hdr->sizeof_hdr, 1);

    /* datatype, bitpix -- each 2 bytes, at offset 12 */
    nifti_header_swap_2(&hdr->datatype, 2);

    /* dim[8] -- 8 * 8 bytes, at offset 16 */
    nifti_header_swap_8(hdr->dim, 8);

    /* intent_p1..p3 -- 3 * 8 bytes, at offset 80 */
    nifti_header_swap_8(&hdr->intent_p1, 3);

    /* pixdim[8] -- 8 * 8 bytes, at offset 104 */
    nifti_header_swap_8(hdr->pixdim, 8);

    /* vox_offset -- 8 bytes, at offset 168 */
    nifti_header_swap_8(&hdr->vox_offset, 1);

    /* scl_slope, scl_inter, cal_max, cal_min -- 4 * 8 bytes, at offset 176 */
    nifti_header_swap_8(&hdr->scl_slope, 4);

    /* slice_duration, toffset -- 2 * 8 bytes, at offset 208 */
    nifti_header_swap_8(&hdr->slice_duration, 2);

    /* slice_start, slice_end -- 2 * 8 bytes, at offset 224 */
    nifti_header_swap_8(&hdr->slice_start, 2);

    /* qform_code, sform_code -- 2 * 4 bytes, at offset 344 */
    nifti_header_swap_4(&hdr->qform_code, 2);

    /* quatern_b..d -- 3 * 8 bytes, at offset 352 */
    nifti_header_swap_8(&hdr->quatern_b, 3);

    /* qoffset_x..z -- 3 * 8 bytes, at offset 376 */
    nifti_header_swap_8(&hdr->qoffset_x, 3);

    /* srow_x[4], srow_y[4], srow_z[4] -- 12 * 8 bytes, at offset 400 */
    nifti_header_swap_8(hdr->srow_x, 12);

    /* slice_code, xyzt_units, intent_code -- 3 * 4 bytes, at offset 496 */
    nifti_header_swap_4(&hdr->slice_code, 3);
}

/* ------------------------------------------------------------------
 *  nifti_hdr1_looks_good
 * ------------------------------------------------------------------ */
int nifti_hdr1_looks_good(const nifti_1_header *hdr) {
    int c, ndim;

    if (!hdr) return 0;

    /* sizeof_hdr must be 348 (or swapped equivalent - caller should
       have already ensured correct byte order) */
    if (hdr->sizeof_hdr != 348) return 0;

    /* magic must be "ni1\0" or "n+1\0" */
    if (!((strncmp(hdr->magic, "ni1", 3) == 0 ||
           strncmp(hdr->magic, "n+1", 3) == 0) &&
          hdr->magic[3] == '\0')) {
        return 0;
    }

    /* check that dimensions are reasonable */
    ndim = hdr->dim[0];
    if (ndim < 1 || ndim > 7) return 0;
    for (c = 1; c <= ndim; c++) {
        if (hdr->dim[c] <= 0) return 0;
    }
    for (c = ndim + 1; c <= 7; c++) {
        if (hdr->dim[c] != 0) return 0;
    }

    /* check that pixdim values are reasonable (non-zero for used dims) */
    if (hdr->pixdim[0] != 0.0f) {
        for (c = 1; c <= ndim; c++) {
            if (hdr->pixdim[c] == 0.0f) return 0;
        }
    }

    /* check vox_offset is reasonable */
    if (hdr->vox_offset < 0.0f) return 0;

    return 1;
}

/* ------------------------------------------------------------------
 *  nifti_hdr2_looks_good
 * ------------------------------------------------------------------ */
int nifti_hdr2_looks_good(const nifti_2_header *hdr) {
    int c, ndim;

    if (!hdr) return 0;

    if (hdr->sizeof_hdr != 540) return 0;

    if (!(strncmp(hdr->magic, "n+2", 3) == 0 ||
          strncmp(hdr->magic, "ni2", 3) == 0)) {
        return 0;
    }

    ndim = (int)hdr->dim[0];
    if (ndim < 1 || ndim > 7) return 0;
    for (c = 1; c <= ndim; c++) {
        if (hdr->dim[c] <= 0) return 0;
    }
    for (c = ndim + 1; c <= 7; c++) {
        if (hdr->dim[c] != 0) return 0;
    }

    if (hdr->vox_offset < 0) return 0;

    return 1;
}

/* ------------------------------------------------------------------
 *  nifti_read_header_1
 * ------------------------------------------------------------------ */
nifti_1_header *nifti_read_header_1(const char *fname, int *needs_swap) {
    znzFile fp;
    nifti_1_header *hdr;
    int use_comp, swap;

    if (!fname) return NULL;

    hdr = (nifti_1_header *)malloc(sizeof(nifti_1_header));
    if (!hdr) return NULL;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) {
        free(hdr);
        return NULL;
    }

    if (znzread(hdr, sizeof(nifti_1_header), 1, fp) != 1) {
        znzclose(&fp);
        free(hdr);
        return NULL;
    }

    znzclose(&fp);

    swap = nifti_needs_swap_1(hdr);
    if (swap < 0) {
        /* unrecognised sizeof_hdr — corrupted? */
        free(hdr);
        return NULL;
    }

    if (swap == 1) {
        nifti_swap_header_1(hdr);
    }

    if (needs_swap) *needs_swap = swap;

    return hdr;
}

/* ------------------------------------------------------------------
 *  nifti_read_header_2
 * ------------------------------------------------------------------ */
nifti_2_header *nifti_read_header_2(const char *fname, int *needs_swap) {
    znzFile fp;
    nifti_2_header *hdr;
    int use_comp, swap;

    if (!fname) return NULL;

    hdr = (nifti_2_header *)malloc(sizeof(nifti_2_header));
    if (!hdr) return NULL;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) {
        free(hdr);
        return NULL;
    }

    if (znzread(hdr, sizeof(nifti_2_header), 1, fp) != 1) {
        znzclose(&fp);
        free(hdr);
        return NULL;
    }

    znzclose(&fp);

    swap = nifti_needs_swap_2(hdr);
    if (swap < 0) {
        free(hdr);
        return NULL;
    }

    if (swap == 1) {
        nifti_swap_header_2(hdr);
    }

    if (needs_swap) *needs_swap = swap;

    return hdr;
}

/* ------------------------------------------------------------------
 *  nifti_write_header_1
 * ------------------------------------------------------------------ */
int nifti_write_header_1(const char *fname, const nifti_1_header *hdr) {
    znzFile fp;
    int use_comp;
    size_t written;

    if (!fname || !hdr) return -1;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "wb", use_comp);
    if (!fp) return -1;

    written = znzwrite(hdr, sizeof(nifti_1_header), 1, fp);
    znzclose(&fp);

    return (written == 1) ? 0 : -1;
}

/* ------------------------------------------------------------------
 *  nifti_write_header_2
 * ------------------------------------------------------------------ */
int nifti_write_header_2(const char *fname, const nifti_2_header *hdr) {
    znzFile fp;
    int use_comp;
    size_t written;

    if (!fname || !hdr) return -1;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "wb", use_comp);
    if (!fp) return -1;

    written = znzwrite(hdr, sizeof(nifti_2_header), 1, fp);
    znzclose(&fp);

    return (written == 1) ? 0 : -1;
}

/* ------------------------------------------------------------------
 *  nifti_read_extensions
 *
 *  For NIfTI-1: extensions start at byte 352 (after the 348-byte
 *  header + 4-byte extender) and continue up to vox_offset.
 *  For NIfTI-2: extensions start at byte 544 (after the 540-byte
 *  header + 4-byte extender) and continue up to vox_offset.
 *
 *  This function auto-detects NIfTI-1 vs NIfTI-2 by reading the
 *  first 4 bytes (sizeof_hdr).  ext_list is allocated and *num_ext
 *  is set to the count of extensions found.
 * ------------------------------------------------------------------ */
int nifti_read_extensions(const char *fname, int *num_ext,
                          nifti1_extension **ext_list) {
    znzFile fp;
    int use_comp, is_nifti2;
    int32_t sizeof_hdr = 0;
    long long vox_offset_ll;
    long long file_bytes;
    long long ext_start;
    long long pos;
    int    count = 0;
    nifti1_extension *list = NULL;

    if (!fname || !num_ext || !ext_list) return -1;

    *num_ext  = 0;
    *ext_list = NULL;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "rb", use_comp);
    if (!fp) return -1;

    /* --- determine whether NIfTI-1 or NIfTI-2 --- */
    if (znzread(&sizeof_hdr, 4, 1, fp) != 1) {
        znzclose(&fp);
        return -1;
    }
    znzrewind(fp);

    /* may need to byteswap sizeof_hdr to figure out the version */
    if (sizeof_hdr == 348 || sizeof_hdr == 1543569408) {
        is_nifti2 = 0;
    } else if (sizeof_hdr == 540 || sizeof_hdr == 469893120) {
        is_nifti2 = 1;
    } else {
        znzclose(&fp);
        return -1;
    }

    /* --- read vox_offset --- */
    if (is_nifti2) {
        nifti_2_header h2;
        if (znzread(&h2, sizeof(nifti_2_header), 1, fp) != 1) {
            znzclose(&fp);
            return -1;
        }
        /* the header might need swapping */
        if (h2.sizeof_hdr == 469893120) {
            nifti_swap_header_2(&h2);
        }
        vox_offset_ll = (long long)h2.vox_offset;
    } else {
        nifti_1_header h1;
        if (znzread(&h1, sizeof(nifti_1_header), 1, fp) != 1) {
            znzclose(&fp);
            return -1;
        }
        if (h1.sizeof_hdr == 1543569408) {
            nifti_swap_header_1(&h1);
        }
        vox_offset_ll = (long long)h1.vox_offset;
    }

    ext_start = is_nifti2 ? 544LL : 352LL;

    /* if vox_offset is 0, no extensions (data starts right away) */
    if (vox_offset_ll == 0) {
        znzclose(&fp);
        return 0;
    }

    /* get file size */
    znzseek(fp, 0, SEEK_END);
    file_bytes = znztell(fp);
    if (file_bytes < 0) {
        znzclose(&fp);
        return -1;
    }

    /* seek to extension area */
    if (znzseek(fp, ext_start, SEEK_SET) < 0) {
        znzclose(&fp);
        return -1;
    }

    pos = ext_start;

    /* --- read extensions one by one --- */
    while (pos + 8 <= vox_offset_ll && pos + 8 <= file_bytes) {
        int esize, ecode;

        if (znzread(&esize, 4, 1, fp) != 1) break;
        if (znzread(&ecode, 4, 1, fp) != 1) break;

        /* esize / ecode may be swapped — handle both.  For simplicity
           we rely on the fact that the header byte order tells us the
           extension byte order.  Since we already determined swap
           status from sizeof_hdr above, we apply the same logic here. */
        {
            int native_swap = (is_nifti2
                ? (sizeof_hdr == 469893120 ? 1 : 0)
                : (sizeof_hdr == 1543569408 ? 1 : 0));

            if (native_swap) {
                unsigned char *p;
                p = (unsigned char *)&esize;
                { unsigned char t; t=p[0]; p[0]=p[3]; p[3]=t; t=p[1]; p[1]=p[2]; p[2]=t; }
                p = (unsigned char *)&ecode;
                { unsigned char t; t=p[0]; p[0]=p[3]; p[3]=t; t=p[1]; p[1]=p[2]; p[2]=t; }
            }
        }

        if (esize < 8) break;          /* extension too small  */
        if (ecode == 0 && esize == 0) break; /* no more extensions */

        /* increase list */
        {
            nifti1_extension *tmp;
            tmp = (nifti1_extension *)realloc(list,
                  (size_t)(count + 1) * sizeof(nifti1_extension));
            if (!tmp) {
                nifti_free_extensions(count, list);
                znzclose(&fp);
                return -1;
            }
            list = tmp;
            list[count].esize = esize;
            list[count].ecode = ecode;
            list[count].edata = (char *)malloc((size_t)(esize - 8));
            if (!list[count].edata) {
                nifti_free_extensions(count + 1, list);
                znzclose(&fp);
                return -1;
            }
            if (znzread(list[count].edata, 1, (size_t)(esize - 8), fp)
                != (size_t)(esize - 8)) {
                nifti_free_extensions(count + 1, list);
                znzclose(&fp);
                return -1;
            }
            count++;
        }

        pos += esize;
        if (pos < ext_start) break; /* overflow guard */
    }

    znzclose(&fp);

    *num_ext  = count;
    *ext_list = list;
    return 0;
}

/* ------------------------------------------------------------------
 *  nifti_write_extensions
 * ------------------------------------------------------------------ */
int nifti_write_extensions(const char *fname, int num_ext,
                           const nifti1_extension *ext_list) {
    znzFile fp;
    int use_comp;
    int i;

    if (!fname) return -1;
    if (num_ext < 0) return -1;
    if (num_ext > 0 && !ext_list) return -1;

    use_comp = nifti_is_gzfile(fname) ? 1 : 0;
    fp = znzopen(fname, "ab", use_comp);
    if (!fp) return -1;

    for (i = 0; i < num_ext; i++) {
        if (znzwrite(&ext_list[i].esize, 4, 1, fp) != 1) { znzclose(&fp); return -1; }
        if (znzwrite(&ext_list[i].ecode, 4, 1, fp) != 1) { znzclose(&fp); return -1; }
        if (ext_list[i].esize > 8 && ext_list[i].edata) {
            if (znzwrite(ext_list[i].edata, 1,
                         (size_t)(ext_list[i].esize - 8), fp)
                != (size_t)(ext_list[i].esize - 8)) {
                znzclose(&fp);
                return -1;
            }
        }
    }

    znzclose(&fp);
    return 0;
}

/* ------------------------------------------------------------------
 *  nifti_free_extensions
 * ------------------------------------------------------------------ */
void nifti_free_extensions(int num_ext, nifti1_extension *ext_list) {
    int i;
    if (!ext_list || num_ext <= 0) return;
    for (i = 0; i < num_ext; i++) {
        free(ext_list[i].edata);
        ext_list[i].edata = NULL;
    }
    free(ext_list);
}

/* ------------------------------------------------------------------
 *  nifti_add_extension
 * ------------------------------------------------------------------ */
int nifti_add_extension(int ecode, const char *edata, int esize,
                        int *num_ext, nifti1_extension **ext_list) {
    nifti1_extension *tmp;

    if (!num_ext || !ext_list) return -1;
    if (esize < 0) return -1;
    if (esize > 0 && !edata) return -1;

    tmp = (nifti1_extension *)realloc(*ext_list,
          (size_t)(*num_ext + 1) * sizeof(nifti1_extension));
    if (!tmp) return -1;

    *ext_list = tmp;
    (*ext_list)[*num_ext].esize = esize + 8;
    (*ext_list)[*num_ext].ecode = ecode;
    (*ext_list)[*num_ext].edata = NULL;

    if (esize > 0) {
        (*ext_list)[*num_ext].edata = (char *)malloc((size_t)esize);
        if (!(*ext_list)[*num_ext].edata) return -1;
        memcpy((*ext_list)[*num_ext].edata, edata, (size_t)esize);
    }

    (*num_ext)++;
    return 0;
}

/* ------------------------------------------------------------------
 *  nifti_disp_header_1
 * ------------------------------------------------------------------ */
void nifti_disp_header_1(const nifti_1_header *hdr) {
    int c;

    if (!hdr) {
        printf("(null header)\n");
        return;
    }

    printf("--- NIfTI-1 Header ---\n");
    printf("sizeof_hdr   : %d\n", (int)hdr->sizeof_hdr);
    printf("magic        : %.4s\n", hdr->magic);
    printf("datatype     : %d (%s)\n", (int)hdr->datatype,
           nifti_datatype_string(hdr->datatype));
    printf("bitpix       : %d\n", (int)hdr->bitpix);
    printf("dim          :");
    for (c = 0; c < 8; c++) printf(" %d", (int)hdr->dim[c]);
    printf("\n");
    printf("pixdim       :");
    for (c = 0; c < 8; c++) printf(" %.4f", (double)hdr->pixdim[c]);
    printf("\n");
    printf("vox_offset   : %.4f\n", (double)hdr->vox_offset);
    printf("scl_slope    : %.6f\n", (double)hdr->scl_slope);
    printf("scl_inter    : %.6f\n", (double)hdr->scl_inter);
    printf("cal_min/max  : %.4f / %.4f\n",
           (double)hdr->cal_min, (double)hdr->cal_max);
    printf("intent_code  : %d\n", (int)hdr->intent_code);
    printf("intent_p1-3  : %.4f %.4f %.4f\n",
           (double)hdr->intent_p1, (double)hdr->intent_p2,
           (double)hdr->intent_p3);
    printf("qform_code   : %d\n", (int)hdr->qform_code);
    printf("sform_code   : %d\n", (int)hdr->sform_code);
    printf("quatern b/c/d: %.4f %.4f %.4f\n",
           (double)hdr->quatern_b, (double)hdr->quatern_c,
           (double)hdr->quatern_d);
    printf("qoffset x/y/z: %.4f %.4f %.4f\n",
           (double)hdr->qoffset_x, (double)hdr->qoffset_y,
           (double)hdr->qoffset_z);
    printf("srow_x       : %.4f %.4f %.4f %.4f\n",
           (double)hdr->srow_x[0], (double)hdr->srow_x[1],
           (double)hdr->srow_x[2], (double)hdr->srow_x[3]);
    printf("srow_y       : %.4f %.4f %.4f %.4f\n",
           (double)hdr->srow_y[0], (double)hdr->srow_y[1],
           (double)hdr->srow_y[2], (double)hdr->srow_y[3]);
    printf("srow_z       : %.4f %.4f %.4f %.4f\n",
           (double)hdr->srow_z[0], (double)hdr->srow_z[1],
           (double)hdr->srow_z[2], (double)hdr->srow_z[3]);
    printf("slice_start  : %d\n", (int)hdr->slice_start);
    printf("slice_end    : %d\n", (int)hdr->slice_end);
    printf("slice_code   : %d\n", (int)hdr->slice_code);
    printf("xyzt_units   : %d\n", (int)hdr->xyzt_units);
    printf("dim_info     : %d\n", (int)hdr->dim_info);
    printf("descrip      : %.80s\n", hdr->descrip);
    printf("aux_file     : %.24s\n", hdr->aux_file);
    printf("intent_name  : %.16s\n", hdr->intent_name);
    printf("extender     : 0x%02x%02x%02x%02x\n",
           (unsigned char)hdr->extender[0],
           (unsigned char)hdr->extender[1],
           (unsigned char)hdr->extender[2],
           (unsigned char)hdr->extender[3]);
}

/* ------------------------------------------------------------------
 *  nifti_disp_header_2
 * ------------------------------------------------------------------ */
void nifti_disp_header_2(const nifti_2_header *hdr) {
    int c;

    if (!hdr) {
        printf("(null header)\n");
        return;
    }

    printf("--- NIfTI-2 Header ---\n");
    printf("sizeof_hdr   : %d\n", (int)hdr->sizeof_hdr);
    printf("magic        : %.8s\n", hdr->magic);
    printf("datatype     : %d (%s)\n", (int)hdr->datatype,
           nifti_datatype_string(hdr->datatype));
    printf("bitpix       : %d\n", (int)hdr->bitpix);
    printf("dim          :");
    for (c = 0; c < 8; c++) printf(" %lld", (long long)hdr->dim[c]);
    printf("\n");
    printf("pixdim       :");
    for (c = 0; c < 8; c++) printf(" %.4f", hdr->pixdim[c]);
    printf("\n");
    printf("vox_offset   : %lld\n", (long long)hdr->vox_offset);
    printf("scl_slope    : %.6f\n", hdr->scl_slope);
    printf("scl_inter    : %.6f\n", hdr->scl_inter);
    printf("cal_min/max  : %.4f / %.4f\n", hdr->cal_min, hdr->cal_max);
    printf("intent_code  : %d\n", (int)hdr->intent_code);
    printf("intent_p1-3  : %.4f %.4f %.4f\n",
           hdr->intent_p1, hdr->intent_p2, hdr->intent_p3);
    printf("qform_code   : %d\n", (int)hdr->qform_code);
    printf("sform_code   : %d\n", (int)hdr->sform_code);
    printf("quatern b/c/d: %.4f %.4f %.4f\n",
           hdr->quatern_b, hdr->quatern_c, hdr->quatern_d);
    printf("qoffset x/y/z: %.4f %.4f %.4f\n",
           hdr->qoffset_x, hdr->qoffset_y, hdr->qoffset_z);
    printf("srow_x       : %.4f %.4f %.4f %.4f\n",
           hdr->srow_x[0], hdr->srow_x[1], hdr->srow_x[2], hdr->srow_x[3]);
    printf("srow_y       : %.4f %.4f %.4f %.4f\n",
           hdr->srow_y[0], hdr->srow_y[1], hdr->srow_y[2], hdr->srow_y[3]);
    printf("srow_z       : %.4f %.4f %.4f %.4f\n",
           hdr->srow_z[0], hdr->srow_z[1], hdr->srow_z[2], hdr->srow_z[3]);
    printf("slice_start  : %lld\n", (long long)hdr->slice_start);
    printf("slice_end    : %lld\n", (long long)hdr->slice_end);
    printf("slice_code   : %d\n", (int)hdr->slice_code);
    printf("xyzt_units   : %d\n", (int)hdr->xyzt_units);
    printf("dim_info     : %d\n", (int)hdr->dim_info);
    printf("descrip      : %.80s\n", hdr->descrip);
    printf("aux_file     : %.24s\n", hdr->aux_file);
    printf("intent_name  : %.16s\n", hdr->intent_name);
}

#endif /* NIFTI_HEADER_IMPLEMENTED */
#endif /* NIFTI_HEADER_IMPLEMENTATION */
