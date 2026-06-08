/*
   NIFTI_BASE.H -- Layer 1: Core types, constants, byte swapping, matrix math
   
   This is a header-only library (STB-style). To use it:
   
     #define NIFTI_BASE_IMPLEMENTATION
     #include "nifti_base.h"
   
   This layer has no dependencies beyond the C standard library.
   
   PUBLIC DOMAIN - No warranty, use at your own risk.
*/

#ifndef NIFTI_BASE_H
#define NIFTI_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 *  BYTE ORDER DETECTION
 *=======================================================================*/
#define NIFTI_LSB_FIRST 1
#define NIFTI_MSB_FIRST 2

static int nifti_short_order(void) {
    /* Returns NIFTI_LSB_FIRST or NIFTI_MSB_FIRST */
    union { unsigned char bb[2]; unsigned short ss; } test;
    test.ss = 1;
    return (test.bb[0] == 1) ? NIFTI_LSB_FIRST : NIFTI_MSB_FIRST;
}

/*=========================================================================
 *  DATATYPE CONSTANTS
 *=======================================================================*/
#define NIFTI_DT_NONE            0
#define NIFTI_DT_BINARY          1
#define NIFTI_DT_UINT8           2
#define NIFTI_DT_INT16           4
#define NIFTI_DT_INT32           8
#define NIFTI_DT_FLOAT32        16
#define NIFTI_DT_COMPLEX64      32
#define NIFTI_DT_FLOAT64        64
#define NIFTI_DT_RGB24         128
#define NIFTI_DT_INT8          256
#define NIFTI_DT_UINT16        512
#define NIFTI_DT_UINT32        768
#define NIFTI_DT_INT64        1024
#define NIFTI_DT_UINT64       1280
#define NIFTI_DT_FLOAT128     1536
#define NIFTI_DT_COMPLEX128   1792
#define NIFTI_DT_COMPLEX256   2048
#define NIFTI_DT_RGBA32       2304
#define NIFTI_DT_ALL           255

/* Friendly aliases */
#define NIFTI_TYPE_UINT8       NIFTI_DT_UINT8
#define NIFTI_TYPE_INT16       NIFTI_DT_INT16
#define NIFTI_TYPE_INT32       NIFTI_DT_INT32
#define NIFTI_TYPE_FLOAT32     NIFTI_DT_FLOAT32
#define NIFTI_TYPE_COMPLEX64   NIFTI_DT_COMPLEX64
#define NIFTI_TYPE_FLOAT64     NIFTI_DT_FLOAT64
#define NIFTI_TYPE_RGB24       NIFTI_DT_RGB24
#define NIFTI_TYPE_INT8        NIFTI_DT_INT8
#define NIFTI_TYPE_UINT16      NIFTI_DT_UINT16
#define NIFTI_TYPE_UINT32      NIFTI_DT_UINT32
#define NIFTI_TYPE_INT64       NIFTI_DT_INT64
#define NIFTI_TYPE_UINT64      NIFTI_DT_UINT64
#define NIFTI_TYPE_FLOAT128    NIFTI_DT_FLOAT128
#define NIFTI_TYPE_COMPLEX128  NIFTI_DT_COMPLEX128
#define NIFTI_TYPE_COMPLEX256  NIFTI_DT_COMPLEX256
#define NIFTI_TYPE_RGBA32      NIFTI_DT_RGBA32

/*=========================================================================
 *  INTENT CODES
 *=======================================================================*/
#define NIFTI_INTENT_NONE        0
#define NIFTI_INTENT_CORREL      2
#define NIFTI_INTENT_TTEST       3
#define NIFTI_INTENT_FTEST       4
#define NIFTI_INTENT_ZSCORE      5
#define NIFTI_INTENT_CHISQ       6
#define NIFTI_INTENT_BETA        7
#define NIFTI_INTENT_BINOM       8
#define NIFTI_INTENT_GAMMA       9
#define NIFTI_INTENT_POISSON    10
#define NIFTI_INTENT_NORMAL     11
#define NIFTI_INTENT_FTEST_NONC 12
#define NIFTI_INTENT_CHISQ_NONC 13
#define NIFTI_INTENT_LOGISTIC   14
#define NIFTI_INTENT_LAPLACE    15
#define NIFTI_INTENT_UNIFORM    16
#define NIFTI_INTENT_TTEST_NONC 17
#define NIFTI_INTENT_WEIBULL    18
#define NIFTI_INTENT_CHI        19
#define NIFTI_INTENT_INVGAUSS   20
#define NIFTI_INTENT_EXTVAL     21
#define NIFTI_INTENT_PVAL       22
#define NIFTI_INTENT_LOGPVAL    23
#define NIFTI_INTENT_LOG10PVAL  24
#define NIFTI_INTENT_ESTIMATE  1001
#define NIFTI_INTENT_LABEL     1002
#define NIFTI_INTENT_NEURONAME 1003
#define NIFTI_INTENT_GENMATRIX 1004
#define NIFTI_INTENT_SYMMATRIX 1005
#define NIFTI_INTENT_DISPVECT  1006
#define NIFTI_INTENT_VECTOR    1007
#define NIFTI_INTENT_POINTSET  1008
#define NIFTI_INTENT_TRIANGLE  1009
#define NIFTI_INTENT_QUATERNION 1010
#define NIFTI_INTENT_DIMLESS   1011
#define NIFTI_INTENT_TIME_SERIES 2000
#define NIFTI_INTENT_NODE_INDEX 2001
#define NIFTI_INTENT_RGB_VECTOR 2002
#define NIFTI_INTENT_RGBA_VECTOR 2003
#define NIFTI_INTENT_SHAPE      2004

/*=========================================================================
 *  TRANSFORM CODES
 *=======================================================================*/
#define NIFTI_XFORM_UNKNOWN      0
#define NIFTI_XFORM_SCANNER_ANAT 1
#define NIFTI_XFORM_ALIGNED_ANAT 2
#define NIFTI_XFORM_TALAIRACH    3
#define NIFTI_XFORM_MNI_152      4

/*=========================================================================
 *  UNIT CODES (for xyzt_units)
 *=======================================================================*/
#define NIFTI_UNITS_UNKNOWN 0
#define NIFTI_UNITS_METER   1
#define NIFTI_UNITS_MM      2
#define NIFTI_UNITS_MICRON  3
#define NIFTI_UNITS_SEC     8
#define NIFTI_UNITS_MSEC   16
#define NIFTI_UNITS_USEC   24
#define NIFTI_UNITS_HZ     32
#define NIFTI_UNITS_PPM    40
#define NIFTI_UNITS_RADS   48

#define NIFTI_SPATIAL_UNITS_MASK 7
#define NIFTI_TEMPORAL_UNITS_MASK 56  /* 0x38 */

/*=========================================================================
 *  SLICE ORDER CODES
 *=======================================================================*/
#define NIFTI_SLICE_UNKNOWN  0
#define NIFTI_SLICE_SEQ_INC  1
#define NIFTI_SLICE_SEQ_DEC  2
#define NIFTI_SLICE_ALT_INC  3
#define NIFTI_SLICE_ALT_DEC  4
#define NIFTI_SLICE_ALT_INC2 5
#define NIFTI_SLICE_ALT_DEC2 6

/*=========================================================================
 *  FILE TYPE CODES
 *=======================================================================*/
#define NIFTI_FTYPE_ANALYZE   0
#define NIFTI_FTYPE_NIFTI1_1  1
#define NIFTI_FTYPE_NIFTI1_2  2
#define NIFTI_FTYPE_ASCII     3
#define NIFTI_FTYPE_NIFTI2_1  4
#define NIFTI_FTYPE_NIFTI2_2  5

/*=========================================================================
 *  ORIENTATION CODES
 *=======================================================================*/
#define NIFTI_L2R 1  /* Left to Right */
#define NIFTI_R2L 2  /* Right to Left */
#define NIFTI_P2A 3  /* Posterior to Anterior */
#define NIFTI_A2P 4  /* Anterior to Posterior */
#define NIFTI_I2S 5  /* Inferior to Superior */
#define NIFTI_S2I 6  /* Superior to Inferior */

/*=========================================================================
 *  COMPLEX AND RGB TYPES
 *=======================================================================*/
typedef struct { float r, i; } nifti_complex_float;
typedef struct { double r, i; } nifti_complex_double;
typedef struct { long double r, i; } nifti_complex_longdouble;
typedef struct { unsigned char r, g, b; } nifti_rgb_byte;
typedef struct { unsigned char r, g, b, a; } nifti_rgba_byte;

/*=========================================================================
 *  MATRIX TYPES
 *=======================================================================*/
typedef struct { float m[4][4]; } nifti_mat44;
typedef struct { float m[3][3]; } nifti_mat33;
typedef struct { double m[4][4]; } nifti_dmat44;
typedef struct { double m[3][3]; } nifti_dmat33;

/*=========================================================================
 *  DATATYPE SIZE TABLE ENTRY
 *=======================================================================*/
typedef struct {
    int type;       /* NIFTI_DT_* code */
    int nbyper;     /* bytes per element */
    int swapsize;   /* size of swap unit */
    const char *name;
} nifti_type_ele;

/*=========================================================================
 *  DECLARATIONS
 *=======================================================================*/

/* byte swapping */
void nifti_swap_2bytes(size_t n, void *ar);
void nifti_swap_4bytes(size_t n, void *ar);
void nifti_swap_8bytes(size_t n, void *ar);
void nifti_swap_16bytes(size_t n, void *ar);
void nifti_swap_Nbytes(size_t n, int siz, void *ar);

/* datatype queries */
int  nifti_is_valid_datatype(int dtype);
int  nifti_is_inttype(int dtype);
int  nifti_datatype_bytes(int dtype);       /* bytes per element */
int  nifti_datatype_swapsize(int dtype);    /* swap unit size */
const char *nifti_datatype_string(int dtype);
int  nifti_datatype_from_string(const char *str);

/* string queries */
const char *nifti_units_string(int units);
const char *nifti_intent_string(int intent);
const char *nifti_xform_string(int xform);
const char *nifti_slice_string(int slice);
const char *nifti_orientation_string(int orientation);

/* matrix math */
nifti_mat44 nifti_mat44_identity(void);
nifti_mat44 nifti_mat44_inverse(nifti_mat44 R);
nifti_mat44 nifti_mat44_mul(nifti_mat44 A, nifti_mat44 B);
nifti_mat33 nifti_mat33_inverse(nifti_mat33 R);
nifti_mat33 nifti_mat33_mul(nifti_mat33 A, nifti_mat33 B);
nifti_mat33 nifti_mat33_polar(nifti_mat33 A);
float        nifti_mat33_determ(nifti_mat33 R);
float        nifti_mat33_rownorm(nifti_mat33 A);
float        nifti_mat33_colnorm(nifti_mat33 A);

nifti_dmat44 nifti_dmat44_identity(void);
nifti_dmat44 nifti_dmat44_inverse(nifti_dmat44 R);
nifti_dmat44 nifti_dmat44_mul(nifti_dmat44 A, nifti_dmat44 B);
nifti_dmat33 nifti_dmat33_inverse(nifti_dmat33 R);
nifti_dmat33 nifti_dmat33_mul(nifti_dmat33 A, nifti_dmat33 B);
nifti_dmat33 nifti_dmat33_polar(nifti_dmat33 A);
double       nifti_dmat33_determ(nifti_dmat33 R);
double       nifti_dmat33_rownorm(nifti_dmat33 A);
double       nifti_dmat33_colnorm(nifti_dmat33 A);

/* conversion */
nifti_mat44 nifti_dmat44_to_mat44(nifti_dmat44 d);
nifti_dmat44 nifti_mat44_to_dmat44(nifti_mat44 f);

/* quaternion <-> matrix */
void         nifti_mat44_to_quatern(nifti_mat44 R,
              float *qb, float *qc, float *qd,
              float *qx, float *qy, float *qz,
              float *dx, float *dy, float *dz, float *qfac);
nifti_mat44  nifti_quatern_to_mat44(float qb, float qc, float qd,
              float qx, float qy, float qz,
              float dx, float dy, float dz, float qfac);
nifti_mat44  nifti_make_orthog_mat44(float r11, float r12, float r13,
              float r21, float r22, float r23,
              float r31, float r32, float r33);
void         nifti_dmat44_to_quatern(nifti_dmat44 R,
              double *qb, double *qc, double *qd,
              double *qx, double *qy, double *qz,
              double *dx, double *dy, double *dz, double *qfac);
nifti_dmat44 nifti_quatern_to_dmat44(double qb, double qc, double qd,
              double qx, double qy, double qz,
              double dx, double dy, double dz, double qfac);
nifti_dmat44 nifti_make_orthog_dmat44(double r11, double r12, double r13,
              double r21, double r22, double r23,
              double r31, double r32, double r33);

/* orientation from matrix */
int nifti_mat44_to_orientation(nifti_mat44 R, int *icod, int *jcod, int *kcod);
int nifti_dmat44_to_orientation(nifti_dmat44 R, int *icod, int *jcod, int *kcod);

/* utility */
char *nifti_strdup(const char *str);
int   nifti_fileexists(const char *fname);
int   nifti_is_gzfile(const char *fname);

/*=========================================================================
 *  INTERNAL: Datatype table (exposed for transparency)
 *=======================================================================*/
#define NIFTI_NUM_DATATYPES 17
extern const nifti_type_ele nifti_type_table[NIFTI_NUM_DATATYPES];

#ifdef __cplusplus
}
#endif

#endif /* NIFTI_BASE_H */

/*=========================================================================
 *  IMPLEMENTATION
 *=======================================================================*/
#ifdef NIFTI_BASE_IMPLEMENTATION
#ifndef NIFTI_BASE_IMPLEMENTED
#define NIFTI_BASE_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/*----------------------------------------------------------------------
 *  Datatype table
 *--------------------------------------------------------------------*/
const nifti_type_ele nifti_type_table[NIFTI_NUM_DATATYPES] = {
    {NIFTI_DT_NONE,       0, 0, "NIFTI_TYPE_UNKNOWN"},
    {NIFTI_DT_BINARY,     1, 0, "NIFTI_TYPE_BINARY"},
    {NIFTI_DT_UINT8,      1, 0, "NIFTI_TYPE_UINT8"},
    {NIFTI_DT_INT16,      2, 2, "NIFTI_TYPE_INT16"},
    {NIFTI_DT_INT32,      4, 4, "NIFTI_TYPE_INT32"},
    {NIFTI_DT_FLOAT32,    4, 4, "NIFTI_TYPE_FLOAT32"},
    {NIFTI_DT_COMPLEX64,  8, 4, "NIFTI_TYPE_COMPLEX64"},
    {NIFTI_DT_FLOAT64,    8, 8, "NIFTI_TYPE_FLOAT64"},
    {NIFTI_DT_RGB24,      3, 0, "NIFTI_TYPE_RGB24"},
    {NIFTI_DT_INT8,       1, 0, "NIFTI_TYPE_INT8"},
    {NIFTI_DT_UINT16,     2, 2, "NIFTI_TYPE_UINT16"},
    {NIFTI_DT_UINT32,     4, 4, "NIFTI_TYPE_UINT32"},
    {NIFTI_DT_INT64,      8, 8, "NIFTI_TYPE_INT64"},
    {NIFTI_DT_UINT64,     8, 8, "NIFTI_TYPE_UINT64"},
    {NIFTI_DT_FLOAT128,  16, 16, "NIFTI_TYPE_FLOAT128"},
    {NIFTI_DT_COMPLEX128,16, 8, "NIFTI_TYPE_COMPLEX128"},
    {NIFTI_DT_COMPLEX256,32, 16, "NIFTI_TYPE_COMPLEX256"}
};

static const nifti_type_ele *nifti_find_type(int dtype) {
    int i;
    for (i = 0; i < NIFTI_NUM_DATATYPES; i++) {
        if (nifti_type_table[i].type == dtype) return &nifti_type_table[i];
    }
    return NULL;
}

int nifti_is_valid_datatype(int dtype) {
    return nifti_find_type(dtype) != NULL;
}

int nifti_is_inttype(int dtype) {
    switch (dtype) {
        case NIFTI_DT_UINT8:  case NIFTI_DT_INT16:
        case NIFTI_DT_INT32:  case NIFTI_DT_INT8:
        case NIFTI_DT_UINT16: case NIFTI_DT_UINT32:
        case NIFTI_DT_INT64:  case NIFTI_DT_UINT64:
            return 1;
        default:
            return 0;
    }
}

int nifti_datatype_bytes(int dtype) {
    const nifti_type_ele *t = nifti_find_type(dtype);
    return t ? t->nbyper : 0;
}

int nifti_datatype_swapsize(int dtype) {
    const nifti_type_ele *t = nifti_find_type(dtype);
    return t ? t->swapsize : 0;
}

const char *nifti_datatype_string(int dtype) {
    const nifti_type_ele *t = nifti_find_type(dtype);
    return t ? t->name : "NIFTI_TYPE_UNKNOWN";
}

int nifti_datatype_from_string(const char *str) {
    int i;
    for (i = 0; i < NIFTI_NUM_DATATYPES; i++) {
        if (strcmp(nifti_type_table[i].name, str) == 0)
            return nifti_type_table[i].type;
    }
    return -1;
}

/*----------------------------------------------------------------------
 *  Byte swapping
 *--------------------------------------------------------------------*/
void nifti_swap_2bytes(size_t n, void *ar) {
    size_t i;
    unsigned char *p = (unsigned char *)ar;
    for (i = 0; i < n; i++, p += 2) {
        unsigned char t = p[0]; p[0] = p[1]; p[1] = t;
    }
}

void nifti_swap_4bytes(size_t n, void *ar) {
    size_t i;
    unsigned char *p = (unsigned char *)ar;
    for (i = 0; i < n; i++, p += 4) {
        unsigned char t;
        t = p[0]; p[0] = p[3]; p[3] = t;
        t = p[1]; p[1] = p[2]; p[2] = t;
    }
}

void nifti_swap_8bytes(size_t n, void *ar) {
    size_t i;
    unsigned char *p = (unsigned char *)ar;
    for (i = 0; i < n; i++, p += 8) {
        unsigned char t;
        t = p[0]; p[0] = p[7]; p[7] = t;
        t = p[1]; p[1] = p[6]; p[6] = t;
        t = p[2]; p[2] = p[5]; p[5] = t;
        t = p[3]; p[3] = p[4]; p[4] = t;
    }
}

void nifti_swap_16bytes(size_t n, void *ar) {
    size_t i, j;
    unsigned char *p = (unsigned char *)ar;
    for (i = 0; i < n; i++, p += 16) {
        for (j = 0; j < 8; j++) {
            unsigned char t = p[j]; p[j] = p[15-j]; p[15-j] = t;
        }
    }
}

void nifti_swap_Nbytes(size_t n, int siz, void *ar) {
    switch (siz) {
        case 2: nifti_swap_2bytes(n, ar); break;
        case 4: nifti_swap_4bytes(n, ar); break;
        case 8: nifti_swap_8bytes(n, ar); break;
        case 16: nifti_swap_16bytes(n, ar); break;
        default: break;
    }
}

/*----------------------------------------------------------------------
 *  String query functions
 *--------------------------------------------------------------------*/
const char *nifti_units_string(int units) {
    switch (units) {
        case NIFTI_UNITS_METER:  return "m";
        case NIFTI_UNITS_MM:     return "mm";
        case NIFTI_UNITS_MICRON: return "um";
        case NIFTI_UNITS_SEC:    return "s";
        case NIFTI_UNITS_MSEC:   return "ms";
        case NIFTI_UNITS_USEC:   return "us";
        case NIFTI_UNITS_HZ:     return "Hz";
        case NIFTI_UNITS_PPM:    return "ppm";
        case NIFTI_UNITS_RADS:   return "rad/s";
        default:                 return "Unknown";
    }
}

const char *nifti_intent_string(int intent) {
    switch (intent) {
        case NIFTI_INTENT_NONE:        return "None";
        case NIFTI_INTENT_CORREL:      return "Correlation statistic";
        case NIFTI_INTENT_TTEST:       return "T-statistic";
        case NIFTI_INTENT_FTEST:       return "F-statistic";
        case NIFTI_INTENT_ZSCORE:      return "Z-score";
        case NIFTI_INTENT_CHISQ:       return "Chi-squared distribution";
        case NIFTI_INTENT_BETA:        return "Beta distribution";
        case NIFTI_INTENT_BINOM:       return "Binomial distribution";
        case NIFTI_INTENT_GAMMA:       return "Gamma distribution";
        case NIFTI_INTENT_POISSON:     return "Poisson distribution";
        case NIFTI_INTENT_NORMAL:      return "Normal distribution";
        case NIFTI_INTENT_FTEST_NONC:  return "Noncentral F-statistic";
        case NIFTI_INTENT_CHISQ_NONC:  return "Noncentral chi-squared";
        case NIFTI_INTENT_LOGISTIC:    return "Logistic distribution";
        case NIFTI_INTENT_LAPLACE:     return "Laplace distribution";
        case NIFTI_INTENT_UNIFORM:     return "Uniform distribution";
        case NIFTI_INTENT_TTEST_NONC:  return "Noncentral T-statistic";
        case NIFTI_INTENT_WEIBULL:     return "Weibull distribution";
        case NIFTI_INTENT_CHI:         return "Chi distribution";
        case NIFTI_INTENT_INVGAUSS:    return "Inverse Gaussian";
        case NIFTI_INTENT_EXTVAL:      return "Extreme Value Type I";
        case NIFTI_INTENT_PVAL:        return "P-value";
        case NIFTI_INTENT_LOGPVAL:     return "Log P-value";
        case NIFTI_INTENT_LOG10PVAL:   return "Log10 P-value";
        case NIFTI_INTENT_ESTIMATE:    return "Estimate";
        case NIFTI_INTENT_LABEL:       return "Label index";
        case NIFTI_INTENT_NEURONAME:   return "NeuroNames index";
        case NIFTI_INTENT_GENMATRIX:   return "General matrix";
        case NIFTI_INTENT_SYMMATRIX:   return "Symmetric matrix";
        case NIFTI_INTENT_DISPVECT:    return "Displacement vector";
        case NIFTI_INTENT_VECTOR:      return "Vector";
        case NIFTI_INTENT_POINTSET:    return "Pointset";
        case NIFTI_INTENT_TRIANGLE:    return "Triangle";
        case NIFTI_INTENT_QUATERNION:  return "Quaternion";
        case NIFTI_INTENT_DIMLESS:     return "Dimensionless";
        case NIFTI_INTENT_TIME_SERIES: return "Time series";
        case NIFTI_INTENT_NODE_INDEX:  return "Node index";
        case NIFTI_INTENT_RGB_VECTOR:  return "RGB vector";
        case NIFTI_INTENT_RGBA_VECTOR: return "RGBA vector";
        case NIFTI_INTENT_SHAPE:       return "Shape";
        default:                       return "Unknown intent";
    }
}

const char *nifti_xform_string(int xform) {
    switch (xform) {
        case NIFTI_XFORM_UNKNOWN:       return "Unknown";
        case NIFTI_XFORM_SCANNER_ANAT:  return "Scanner Anat";
        case NIFTI_XFORM_ALIGNED_ANAT:  return "Aligned Anat";
        case NIFTI_XFORM_TALAIRACH:     return "Talairach";
        case NIFTI_XFORM_MNI_152:       return "MNI_152";
        default:                        return "Unknown";
    }
}

const char *nifti_slice_string(int slice) {
    switch (slice) {
        case NIFTI_SLICE_SEQ_INC:  return "sequential_increasing";
        case NIFTI_SLICE_SEQ_DEC:  return "sequential_decreasing";
        case NIFTI_SLICE_ALT_INC:  return "alternating_increasing";
        case NIFTI_SLICE_ALT_DEC:  return "alternating_decreasing";
        case NIFTI_SLICE_ALT_INC2: return "alternating_increasing_2";
        case NIFTI_SLICE_ALT_DEC2: return "alternating_decreasing_2";
        default:                   return "Unknown";
    }
}

const char *nifti_orientation_string(int orientation) {
    switch (orientation) {
        case NIFTI_L2R: return "Left-to-Right";
        case NIFTI_R2L: return "Right-to-Left";
        case NIFTI_P2A: return "Posterior-to-Anterior";
        case NIFTI_A2P: return "Anterior-to-Posterior";
        case NIFTI_I2S: return "Inferior-to-Superior";
        case NIFTI_S2I: return "Superior-to-Inferior";
        default:        return "Unknown";
    }
}

/*----------------------------------------------------------------------
 *  Matrix math -- float (legacy NIfTI-1)
 *--------------------------------------------------------------------*/
nifti_mat44 nifti_mat44_identity(void) {
    nifti_mat44 M;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            M.m[i][j] = (i == j) ? 1.0f : 0.0f;
    return M;
}

nifti_mat44 nifti_mat44_inverse(nifti_mat44 R) {
    /* Specialized inverse for affine matrix [R3x3 | t; 0 0 0 1] */
    nifti_mat44 M;
    nifti_mat33 Q, Qi;
    int i, j;

    /* Extract 3x3 rotation/scaling */
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            Q.m[i][j] = R.m[i][j];
    
    Qi = nifti_mat33_inverse(Q);
    
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            M.m[i][j] = Qi.m[i][j];
        }
        /* Translation part: -R^-1 * t */
        M.m[i][3] = -(Qi.m[i][0] * R.m[0][3] + 
                       Qi.m[i][1] * R.m[1][3] + 
                       Qi.m[i][2] * R.m[2][3]);
        M.m[3][i] = 0.0f;
    }
    M.m[3][3] = 1.0f;
    return M;
}

nifti_mat44 nifti_mat44_mul(nifti_mat44 A, nifti_mat44 B) {
    nifti_mat44 C;
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            C.m[i][j] = 0.0f;
            for (k = 0; k < 4; k++)
                C.m[i][j] += A.m[i][k] * B.m[k][j];
        }
    }
    return C;
}

nifti_mat33 nifti_mat33_inverse(nifti_mat33 R) {
    nifti_mat33 M;
    float det = nifti_mat33_determ(R);
    if (fabsf(det) < FLT_EPSILON) {
        /* Return identity if singular */
        int i, j;
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                M.m[i][j] = (i == j) ? 1.0f : 0.0f;
        return M;
    }
    M.m[0][0] = (R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1]) / det;
    M.m[0][1] = (R.m[0][2]*R.m[2][1] - R.m[0][1]*R.m[2][2]) / det;
    M.m[0][2] = (R.m[0][1]*R.m[1][2] - R.m[0][2]*R.m[1][1]) / det;
    M.m[1][0] = (R.m[1][2]*R.m[2][0] - R.m[1][0]*R.m[2][2]) / det;
    M.m[1][1] = (R.m[0][0]*R.m[2][2] - R.m[0][2]*R.m[2][0]) / det;
    M.m[1][2] = (R.m[0][2]*R.m[1][0] - R.m[0][0]*R.m[1][2]) / det;
    M.m[2][0] = (R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]) / det;
    M.m[2][1] = (R.m[0][1]*R.m[2][0] - R.m[0][0]*R.m[2][1]) / det;
    M.m[2][2] = (R.m[0][0]*R.m[1][1] - R.m[0][1]*R.m[1][0]) / det;
    return M;
}

nifti_mat33 nifti_mat33_mul(nifti_mat33 A, nifti_mat33 B) {
    nifti_mat33 C;
    int i, j, k;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            C.m[i][j] = 0.0f;
            for (k = 0; k < 3; k++)
                C.m[i][j] += A.m[i][k] * B.m[k][j];
        }
    }
    return C;
}

float nifti_mat33_determ(nifti_mat33 R) {
    return R.m[0][0]*(R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1])
         - R.m[0][1]*(R.m[1][0]*R.m[2][2] - R.m[1][2]*R.m[2][0])
         + R.m[0][2]*(R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]);
}

float nifti_mat33_rownorm(nifti_mat33 A) {
    float r1 = sqrtf(A.m[0][0]*A.m[0][0] + A.m[0][1]*A.m[0][1] + A.m[0][2]*A.m[0][2]);
    float r2 = sqrtf(A.m[1][0]*A.m[1][0] + A.m[1][1]*A.m[1][1] + A.m[1][2]*A.m[1][2]);
    float r3 = sqrtf(A.m[2][0]*A.m[2][0] + A.m[2][1]*A.m[2][1] + A.m[2][2]*A.m[2][2]);
    if (r1 < r2) r1 = r2;
    if (r1 < r3) r1 = r3;
    return r1;
}

float nifti_mat33_colnorm(nifti_mat33 A) {
    float c1 = sqrtf(A.m[0][0]*A.m[0][0] + A.m[1][0]*A.m[1][0] + A.m[2][0]*A.m[2][0]);
    float c2 = sqrtf(A.m[0][1]*A.m[0][1] + A.m[1][1]*A.m[1][1] + A.m[2][1]*A.m[2][1]);
    float c3 = sqrtf(A.m[0][2]*A.m[0][2] + A.m[1][2]*A.m[1][2] + A.m[2][2]*A.m[2][2]);
    if (c1 < c2) c1 = c2;
    if (c1 < c3) c1 = c3;
    return c1;
}

nifti_mat33 nifti_mat33_polar(nifti_mat33 A) {
    /* Compute polar decomposition A = QP, return Q (orthogonal part) */
    nifti_mat33 X = A, Y, Z;
    float dif = 1.0f;
    int iter;
    for (iter = 0; iter < 100 && dif > 0.000001f; iter++) {
        Y = nifti_mat33_inverse(X);
        /* X_new = (X + Y^T) / 2 */
        Z.m[0][0] = (X.m[0][0] + Y.m[0][0]) * 0.5f;
        Z.m[0][1] = (X.m[0][1] + Y.m[1][0]) * 0.5f;
        Z.m[0][2] = (X.m[0][2] + Y.m[2][0]) * 0.5f;
        Z.m[1][0] = (X.m[1][0] + Y.m[0][1]) * 0.5f;
        Z.m[1][1] = (X.m[1][1] + Y.m[1][1]) * 0.5f;
        Z.m[1][2] = (X.m[1][2] + Y.m[2][1]) * 0.5f;
        Z.m[2][0] = (X.m[2][0] + Y.m[0][2]) * 0.5f;
        Z.m[2][1] = (X.m[2][1] + Y.m[1][2]) * 0.5f;
        Z.m[2][2] = (X.m[2][2] + Y.m[2][2]) * 0.5f;
        dif = fabsf(Z.m[0][0] - X.m[0][0]) + fabsf(Z.m[0][1] - X.m[0][1]) + fabsf(Z.m[0][2] - X.m[0][2])
            + fabsf(Z.m[1][0] - X.m[1][0]) + fabsf(Z.m[1][1] - X.m[1][1]) + fabsf(Z.m[1][2] - X.m[1][2])
            + fabsf(Z.m[2][0] - X.m[2][0]) + fabsf(Z.m[2][1] - X.m[2][1]) + fabsf(Z.m[2][2] - X.m[2][2]);
        X = Z;
    }
    return X;
}

/*----------------------------------------------------------------------
 *  Matrix math -- double (NIfTI-2)
 *--------------------------------------------------------------------*/
nifti_dmat44 nifti_dmat44_identity(void) {
    nifti_dmat44 M;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            M.m[i][j] = (i == j) ? 1.0 : 0.0;
    return M;
}

nifti_dmat44 nifti_dmat44_inverse(nifti_dmat44 R) {
    nifti_dmat44 M;
    nifti_dmat33 Q, Qi;
    int i, j;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            Q.m[i][j] = R.m[i][j];
    
    Qi = nifti_dmat33_inverse(Q);
    
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            M.m[i][j] = Qi.m[i][j];
        }
        M.m[i][3] = -(Qi.m[i][0] * R.m[0][3] + 
                       Qi.m[i][1] * R.m[1][3] + 
                       Qi.m[i][2] * R.m[2][3]);
        M.m[3][i] = 0.0;
    }
    M.m[3][3] = 1.0;
    return M;
}

nifti_dmat44 nifti_dmat44_mul(nifti_dmat44 A, nifti_dmat44 B) {
    nifti_dmat44 C;
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            C.m[i][j] = 0.0;
            for (k = 0; k < 4; k++)
                C.m[i][j] += A.m[i][k] * B.m[k][j];
        }
    }
    return C;
}

nifti_dmat33 nifti_dmat33_inverse(nifti_dmat33 R) {
    nifti_dmat33 M;
    double det = nifti_dmat33_determ(R);
    if (fabs(det) < DBL_EPSILON) {
        int i, j;
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                M.m[i][j] = (i == j) ? 1.0 : 0.0;
        return M;
    }
    M.m[0][0] = (R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1]) / det;
    M.m[0][1] = (R.m[0][2]*R.m[2][1] - R.m[0][1]*R.m[2][2]) / det;
    M.m[0][2] = (R.m[0][1]*R.m[1][2] - R.m[0][2]*R.m[1][1]) / det;
    M.m[1][0] = (R.m[1][2]*R.m[2][0] - R.m[1][0]*R.m[2][2]) / det;
    M.m[1][1] = (R.m[0][0]*R.m[2][2] - R.m[0][2]*R.m[2][0]) / det;
    M.m[1][2] = (R.m[0][2]*R.m[1][0] - R.m[0][0]*R.m[1][2]) / det;
    M.m[2][0] = (R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]) / det;
    M.m[2][1] = (R.m[0][1]*R.m[2][0] - R.m[0][0]*R.m[2][1]) / det;
    M.m[2][2] = (R.m[0][0]*R.m[1][1] - R.m[0][1]*R.m[1][0]) / det;
    return M;
}

nifti_dmat33 nifti_dmat33_mul(nifti_dmat33 A, nifti_dmat33 B) {
    nifti_dmat33 C;
    int i, j, k;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            C.m[i][j] = 0.0;
            for (k = 0; k < 3; k++)
                C.m[i][j] += A.m[i][k] * B.m[k][j];
        }
    }
    return C;
}

double nifti_dmat33_determ(nifti_dmat33 R) {
    return R.m[0][0]*(R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1])
         - R.m[0][1]*(R.m[1][0]*R.m[2][2] - R.m[1][2]*R.m[2][0])
         + R.m[0][2]*(R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]);
}

double nifti_dmat33_rownorm(nifti_dmat33 A) {
    double r1 = sqrt(A.m[0][0]*A.m[0][0] + A.m[0][1]*A.m[0][1] + A.m[0][2]*A.m[0][2]);
    double r2 = sqrt(A.m[1][0]*A.m[1][0] + A.m[1][1]*A.m[1][1] + A.m[1][2]*A.m[1][2]);
    double r3 = sqrt(A.m[2][0]*A.m[2][0] + A.m[2][1]*A.m[2][1] + A.m[2][2]*A.m[2][2]);
    if (r1 < r2) r1 = r2;
    if (r1 < r3) r1 = r3;
    return r1;
}

double nifti_dmat33_colnorm(nifti_dmat33 A) {
    double c1 = sqrt(A.m[0][0]*A.m[0][0] + A.m[1][0]*A.m[1][0] + A.m[2][0]*A.m[2][0]);
    double c2 = sqrt(A.m[0][1]*A.m[0][1] + A.m[1][1]*A.m[1][1] + A.m[2][1]*A.m[2][1]);
    double c3 = sqrt(A.m[0][2]*A.m[0][2] + A.m[1][2]*A.m[1][2] + A.m[2][2]*A.m[2][2]);
    if (c1 < c2) c1 = c2;
    if (c1 < c3) c1 = c3;
    return c1;
}

nifti_dmat33 nifti_dmat33_polar(nifti_dmat33 A) {
    nifti_dmat33 X = A, Y, Z;
    double dif = 1.0;
    int iter;
    for (iter = 0; iter < 100 && dif > 0.000000000001; iter++) {
        Y = nifti_dmat33_inverse(X);
        Z.m[0][0] = (X.m[0][0] + Y.m[0][0]) * 0.5;
        Z.m[0][1] = (X.m[0][1] + Y.m[1][0]) * 0.5;
        Z.m[0][2] = (X.m[0][2] + Y.m[2][0]) * 0.5;
        Z.m[1][0] = (X.m[1][0] + Y.m[0][1]) * 0.5;
        Z.m[1][1] = (X.m[1][1] + Y.m[1][1]) * 0.5;
        Z.m[1][2] = (X.m[1][2] + Y.m[2][1]) * 0.5;
        Z.m[2][0] = (X.m[2][0] + Y.m[0][2]) * 0.5;
        Z.m[2][1] = (X.m[2][1] + Y.m[1][2]) * 0.5;
        Z.m[2][2] = (X.m[2][2] + Y.m[2][2]) * 0.5;
        dif = fabs(Z.m[0][0] - X.m[0][0]) + fabs(Z.m[0][1] - X.m[0][1]) + fabs(Z.m[0][2] - X.m[0][2])
            + fabs(Z.m[1][0] - X.m[1][0]) + fabs(Z.m[1][1] - X.m[1][1]) + fabs(Z.m[1][2] - X.m[1][2])
            + fabs(Z.m[2][0] - X.m[2][0]) + fabs(Z.m[2][1] - X.m[2][1]) + fabs(Z.m[2][2] - X.m[2][2]);
        X = Z;
    }
    return X;
}

/*----------------------------------------------------------------------
 *  Conversion between float and double matrices
 *--------------------------------------------------------------------*/
nifti_mat44 nifti_dmat44_to_mat44(nifti_dmat44 d) {
    nifti_mat44 f;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            f.m[i][j] = (float)d.m[i][j];
    return f;
}

nifti_dmat44 nifti_mat44_to_dmat44(nifti_mat44 f) {
    nifti_dmat44 d;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            d.m[i][j] = (double)f.m[i][j];
    return d;
}

/*----------------------------------------------------------------------
 *  Quaternion <-> Matrix (float)
 *--------------------------------------------------------------------*/
void nifti_mat44_to_quatern(nifti_mat44 R,
    float *qb, float *qc, float *qd,
    float *qx, float *qy, float *qz,
    float *dx, float *dy, float *dz, float *qfac)
{
    /* Default outputs */
    *qb = *qc = *qd = 0.0f;
    *qx = *qy = *qz = 0.0f;
    *dx = *dy = *dz = 0.0f;
    *qfac = 1.0f;

    /* Compute pixdim */
    float a = sqrtf(R.m[0][0]*R.m[0][0] + R.m[1][0]*R.m[1][0] + R.m[2][0]*R.m[2][0]);
    float b = sqrtf(R.m[0][1]*R.m[0][1] + R.m[1][1]*R.m[1][1] + R.m[2][1]*R.m[2][1]);
    float c = sqrtf(R.m[0][2]*R.m[0][2] + R.m[1][2]*R.m[1][2] + R.m[2][2]*R.m[2][2]);
    float det = R.m[0][0]*(R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1])
        - R.m[0][1]*(R.m[1][0]*R.m[2][2] - R.m[1][2]*R.m[2][0])
        + R.m[0][2]*(R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]);

    if (det < 0) *qfac = -1.0f;

    *dx = a;
    *dy = b;
    *dz = c;

    /* Build normalized rotation matrix */
    float r11 = R.m[0][0] / a;
    float r21 = R.m[1][0] / a;
    float r31 = R.m[2][0] / a;
    float r12 = R.m[0][1] / b;
    float r22 = R.m[1][1] / b;
    float r32 = R.m[2][1] / b;
    float r13 = R.m[0][2] / c * (*qfac);
    float r23 = R.m[1][2] / c * (*qfac);
    float r33 = R.m[2][2] / c * (*qfac);

    /* Extract quaternion from rotation matrix using Shepperd's method */
    {
        float qa, qb2, qc2, qd2;
        float trace_r = r11 + r22 + r33;
        if (trace_r > 0.0f) {
            float s = sqrtf(trace_r + 1.0f) * 2.0f;
            qa = 0.25f * s;
            qb2 = (r32 - r23) / s;
            qc2 = (r13 - r31) / s;
            qd2 = (r21 - r12) / s;
        } else {
            if (r11 > r22 && r11 > r33) {
                float s = sqrtf(1.0f + r11 - r22 - r33) * 2.0f;
                qa = (r32 - r23) / s;
                qb2 = 0.25f * s;
                qc2 = (r12 + r21) / s;
                qd2 = (r13 + r31) / s;
            } else if (r22 > r33) {
                float s = sqrtf(1.0f + r22 - r11 - r33) * 2.0f;
                qa = (r13 - r31) / s;
                qb2 = (r12 + r21) / s;
                qc2 = 0.25f * s;
                qd2 = (r23 + r32) / s;
            } else {
                float s = sqrtf(1.0f + r33 - r11 - r22) * 2.0f;
                qa = (r21 - r12) / s;
                qb2 = (r13 + r31) / s;
                qc2 = (r23 + r32) / s;
                qd2 = 0.25f * s;
            }
        }
        *qb = qb2;
        *qc = qc2;
        *qd = qd2;
        if (qa < 0) { *qb = -(*qb); *qc = -(*qc); *qd = -(*qd); }
    }

    *qx = R.m[0][3];
    *qy = R.m[1][3];
    *qz = R.m[2][3];
}

nifti_mat44 nifti_quatern_to_mat44(float qb, float qc, float qd,
    float qx, float qy, float qz,
    float dx, float dy, float dz, float qfac)
{
    nifti_mat44 M = nifti_mat44_identity();
    float a = 1.0f - (qb*qb + qc*qc + qd*qd);
    float b, c, d;
    if (a < 1e-7f) {
        a = 1.0f / sqrtf(qb*qb + qc*qc + qd*qd);
        b = qb * a; c = qc * a; d = qd * a;
        a = 0.0f;
    } else {
        a = sqrtf(a);
        b = qb; c = qc; d = qd;
    }

    M.m[0][0] = (a*a + b*b - c*c - d*d) * dx;
    M.m[0][1] = (2*b*c - 2*a*d) * dy;
    M.m[0][2] = (2*b*d + 2*a*c) * dz * qfac;
    M.m[1][0] = (2*b*c + 2*a*d) * dx;
    M.m[1][1] = (a*a + c*c - b*b - d*d) * dy;
    M.m[1][2] = (2*c*d - 2*a*b) * dz * qfac;
    M.m[2][0] = (2*b*d - 2*a*c) * dx;
    M.m[2][1] = (2*c*d + 2*a*b) * dy;
    M.m[2][2] = (a*a + d*d - c*c - b*b) * dz * qfac;
    M.m[0][3] = qx;
    M.m[1][3] = qy;
    M.m[2][3] = qz;
    return M;
}

nifti_mat44 nifti_make_orthog_mat44(float r11, float r12, float r13,
    float r21, float r22, float r23,
    float r31, float r32, float r33)
{
    nifti_mat44 M;
    int i;

    /* Normalize columns to get orthogonal matrix */
    float c1 = sqrtf(r11*r11 + r21*r21 + r31*r31);
    float c2 = sqrtf(r12*r12 + r22*r22 + r32*r32);
    float c3 = sqrtf(r13*r13 + r23*r23 + r33*r33);

    if (c1 < 1e-7f) c1 = 1.0f;
    if (c2 < 1e-7f) c2 = 1.0f;
    if (c3 < 1e-7f) c3 = 1.0f;

    M.m[0][0] = r11 / c1; M.m[0][1] = r12 / c2; M.m[0][2] = r13 / c3;
    M.m[1][0] = r21 / c1; M.m[1][1] = r22 / c2; M.m[1][2] = r23 / c3;
    M.m[2][0] = r31 / c1; M.m[2][1] = r32 / c2; M.m[2][2] = r33 / c3;

    for (i = 0; i < 3; i++) {
        M.m[i][3] = 0.0f;
        M.m[3][i] = 0.0f;
    }
    M.m[3][3] = 1.0f;
    return M;
}

/*----------------------------------------------------------------------
 *  Quaternion <-> Matrix (double)
 *--------------------------------------------------------------------*/
void nifti_dmat44_to_quatern(nifti_dmat44 R,
    double *qb, double *qc, double *qd,
    double *qx, double *qy, double *qz,
    double *dx, double *dy, double *dz, double *qfac)
{
    double a = sqrt(R.m[0][0]*R.m[0][0] + R.m[1][0]*R.m[1][0] + R.m[2][0]*R.m[2][0]);
    double b = sqrt(R.m[0][1]*R.m[0][1] + R.m[1][1]*R.m[1][1] + R.m[2][1]*R.m[2][1]);
    double c = sqrt(R.m[0][2]*R.m[0][2] + R.m[1][2]*R.m[1][2] + R.m[2][2]*R.m[2][2]);
    double det = R.m[0][0]*(R.m[1][1]*R.m[2][2] - R.m[1][2]*R.m[2][1])
               - R.m[0][1]*(R.m[1][0]*R.m[2][2] - R.m[1][2]*R.m[2][0])
               + R.m[0][2]*(R.m[1][0]*R.m[2][1] - R.m[1][1]*R.m[2][0]);

    *dx = a; *dy = b; *dz = c;
    *qfac = (det < 0) ? -1.0 : 1.0;

    double r11 = R.m[0][0] / a, r21 = R.m[1][0] / a, r31 = R.m[2][0] / a;
    double r12 = R.m[0][1] / b, r22 = R.m[1][1] / b, r32 = R.m[2][1] / b;
    double r13 = R.m[0][2] / c * (*qfac), r23 = R.m[1][2] / c * (*qfac), r33 = R.m[2][2] / c * (*qfac);

    {
        double qa, qb2, qc2, qd2;
        double trace_r = r11 + r22 + r33;
        if (trace_r > 0.0) {
            double s = sqrt(trace_r + 1.0) * 2.0;
            qa = 0.25 * s;
            qb2 = (r32 - r23) / s;
            qc2 = (r13 - r31) / s;
            qd2 = (r21 - r12) / s;
        } else {
            if (r11 > r22 && r11 > r33) {
                double s = sqrt(1.0 + r11 - r22 - r33) * 2.0;
                qa = (r32 - r23) / s;
                qb2 = 0.25 * s;
                qc2 = (r12 + r21) / s;
                qd2 = (r13 + r31) / s;
            } else if (r22 > r33) {
                double s = sqrt(1.0 + r22 - r11 - r33) * 2.0;
                qa = (r13 - r31) / s;
                qb2 = (r12 + r21) / s;
                qc2 = 0.25 * s;
                qd2 = (r23 + r32) / s;
            } else {
                double s = sqrt(1.0 + r33 - r11 - r22) * 2.0;
                qa = (r21 - r12) / s;
                qb2 = (r13 + r31) / s;
                qc2 = (r23 + r32) / s;
                qd2 = 0.25 * s;
            }
        }
        *qb = qb2; *qc = qc2; *qd = qd2;
        if (qa < 0) { *qb = -(*qb); *qc = -(*qc); *qd = -(*qd); }
    }

    *qx = R.m[0][3]; *qy = R.m[1][3]; *qz = R.m[2][3];
}

nifti_dmat44 nifti_quatern_to_dmat44(double qb, double qc, double qd,
    double qx, double qy, double qz,
    double dx, double dy, double dz, double qfac)
{
    nifti_dmat44 M = nifti_dmat44_identity();
    double a = 1.0 - (qb*qb + qc*qc + qd*qd);
    if (a < 1e-12) {
        double n = 1.0 / sqrt(qb*qb + qc*qc + qd*qd);
        qb *= n; qc *= n; qd *= n; a = 0.0;
    } else {
        a = sqrt(a);
    }

    M.m[0][0] = (a*a + qb*qb - qc*qc - qd*qd) * dx;
    M.m[0][1] = (2*qb*qc - 2*a*qd) * dy;
    M.m[0][2] = (2*qb*qd + 2*a*qc) * dz * qfac;
    M.m[1][0] = (2*qb*qc + 2*a*qd) * dx;
    M.m[1][1] = (a*a + qc*qc - qb*qb - qd*qd) * dy;
    M.m[1][2] = (2*qc*qd - 2*a*qb) * dz * qfac;
    M.m[2][0] = (2*qb*qd - 2*a*qc) * dx;
    M.m[2][1] = (2*qc*qd + 2*a*qb) * dy;
    M.m[2][2] = (a*a + qd*qd - qb*qb - qc*qc) * dz * qfac;
    M.m[0][3] = qx; M.m[1][3] = qy; M.m[2][3] = qz;
    return M;
}

nifti_dmat44 nifti_make_orthog_dmat44(double r11, double r12, double r13,
    double r21, double r22, double r23,
    double r31, double r32, double r33)
{
    nifti_dmat44 M;
    double c1 = sqrt(r11*r11 + r21*r21 + r31*r31);
    double c2 = sqrt(r12*r12 + r22*r22 + r32*r32);
    double c3 = sqrt(r13*r13 + r23*r23 + r33*r33);
    if (c1 < 1e-12) c1 = 1.0; if (c2 < 1e-12) c2 = 1.0; if (c3 < 1e-12) c3 = 1.0;
    M.m[0][0] = r11/c1; M.m[0][1] = r12/c2; M.m[0][2] = r13/c3; M.m[0][3] = 0.0;
    M.m[1][0] = r21/c1; M.m[1][1] = r22/c2; M.m[1][2] = r23/c3; M.m[1][3] = 0.0;
    M.m[2][0] = r31/c1; M.m[2][1] = r32/c2; M.m[2][2] = r33/c3; M.m[2][3] = 0.0;
    M.m[3][0] = 0.0; M.m[3][1] = 0.0; M.m[3][2] = 0.0; M.m[3][3] = 1.0;
    return M;
}

/*----------------------------------------------------------------------
 *  Orientation from matrix
 *--------------------------------------------------------------------*/
int nifti_mat44_to_orientation(nifti_mat44 R, int *icod, int *jcod, int *kcod) {
    /* Column norms */
    float ci = sqrtf(R.m[0][0]*R.m[0][0] + R.m[1][0]*R.m[1][0] + R.m[2][0]*R.m[2][0]);
    float cj = sqrtf(R.m[0][1]*R.m[0][1] + R.m[1][1]*R.m[1][1] + R.m[2][1]*R.m[2][1]);
    float ck = sqrtf(R.m[0][2]*R.m[0][2] + R.m[1][2]*R.m[1][2] + R.m[2][2]*R.m[2][2]);

    if (ci < 1e-7f || cj < 1e-7f || ck < 1e-7f) {
        *icod = *jcod = *kcod = 0;
        return -1;
    }

    /* Determine orientation for each column */
    if      (fabsf(R.m[0][0]/ci) > 0.999f) *icod = (R.m[0][0] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabsf(R.m[1][0]/ci) > 0.999f) *icod = (R.m[1][0] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabsf(R.m[2][0]/ci) > 0.999f) *icod = (R.m[2][0] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *icod = 0;

    if      (fabsf(R.m[0][1]/cj) > 0.999f) *jcod = (R.m[0][1] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabsf(R.m[1][1]/cj) > 0.999f) *jcod = (R.m[1][1] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabsf(R.m[2][1]/cj) > 0.999f) *jcod = (R.m[2][1] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *jcod = 0;

    if      (fabsf(R.m[0][2]/ck) > 0.999f) *kcod = (R.m[0][2] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabsf(R.m[1][2]/ck) > 0.999f) *kcod = (R.m[1][2] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabsf(R.m[2][2]/ck) > 0.999f) *kcod = (R.m[2][2] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *kcod = 0;

    return 0;
}

int nifti_dmat44_to_orientation(nifti_dmat44 R, int *icod, int *jcod, int *kcod) {
    double ci = sqrt(R.m[0][0]*R.m[0][0] + R.m[1][0]*R.m[1][0] + R.m[2][0]*R.m[2][0]);
    double cj = sqrt(R.m[0][1]*R.m[0][1] + R.m[1][1]*R.m[1][1] + R.m[2][1]*R.m[2][1]);
    double ck = sqrt(R.m[0][2]*R.m[0][2] + R.m[1][2]*R.m[1][2] + R.m[2][2]*R.m[2][2]);

    if (ci < 1e-12 || cj < 1e-12 || ck < 1e-12) {
        *icod = *jcod = *kcod = 0;
        return -1;
    }

    if      (fabs(R.m[0][0]/ci) > 0.999) *icod = (R.m[0][0] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabs(R.m[1][0]/ci) > 0.999) *icod = (R.m[1][0] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabs(R.m[2][0]/ci) > 0.999) *icod = (R.m[2][0] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *icod = 0;

    if      (fabs(R.m[0][1]/cj) > 0.999) *jcod = (R.m[0][1] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabs(R.m[1][1]/cj) > 0.999) *jcod = (R.m[1][1] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabs(R.m[2][1]/cj) > 0.999) *jcod = (R.m[2][1] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *jcod = 0;

    if      (fabs(R.m[0][2]/ck) > 0.999) *kcod = (R.m[0][2] > 0) ? NIFTI_L2R : NIFTI_R2L;
    else if (fabs(R.m[1][2]/ck) > 0.999) *kcod = (R.m[1][2] > 0) ? NIFTI_P2A : NIFTI_A2P;
    else if (fabs(R.m[2][2]/ck) > 0.999) *kcod = (R.m[2][2] > 0) ? NIFTI_I2S : NIFTI_S2I;
    else *kcod = 0;

    return 0;
}

/*----------------------------------------------------------------------
 *  Utility
 *--------------------------------------------------------------------*/
char *nifti_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *dup = (char *)malloc(len);
    if (dup) memcpy(dup, str, len);
    return dup;
}

int nifti_fileexists(const char *fname) {
    if (!fname) return 0;
    FILE *fp = fopen(fname, "rb");
    if (fp) { fclose(fp); return 1; }
    return 0;
}

int nifti_is_gzfile(const char *fname) {
    if (!fname) return 0;
    size_t len = strlen(fname);
    return (len > 3 && strcmp(fname + len - 3, ".gz") == 0);
}

#endif /* NIFTI_BASE_IMPLEMENTED */
#endif /* NIFTI_BASE_IMPLEMENTATION */
