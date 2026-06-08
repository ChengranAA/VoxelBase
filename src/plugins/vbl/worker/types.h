#ifndef VBL_TYPES_H
#define VBL_TYPES_H

#include <stdint.h>

typedef enum {
  TYPE_VOLUME4D,
  TYPE_VOLUME3D,
  TYPE_TIMESERIES,
  TYPE_CORRMAP,
  TYPE_MASK,
  TYPE_NIL,
  TYPE_AFFINE,
} VblType;

typedef struct Value {
  VblType type;
  int nx, ny, nz, nt;
  double dx, dy, dz, tr;
  float *data;
  int16_t *idata; /* integer data for masks/labels */
  int is_int;     /* 1 = use idata, 0 = use data */
  int64_t data_len;
  int owns_data;        /* 1 = must free, 0 = view into parent */
  struct Value *parent; /* non-NULL if this is a view */
  int64_t offset;       /* byte offset into parent->data */
  char label[256];
} Value;

Value *val_new_volume4d(int nx, int ny, int nz, int nt, double dx, double dy,
                        double dz, double tr);
Value *val_new_volume3d(int nx, int ny, int nz, double dx, double dy,
                        double dz);
Value *val_new_timeseries(int len, double tr);
Value *val_new_corrmap(int nx, int ny, int nz, double dx, double dy, double dz);
Value *val_new_mask(int nx, int ny, int nz, double dx, double dy, double dz);
Value *val_new_mask_int(int nx, int ny, int nz, double dx, double dy,
                        double dz);
Value *val_new_nil(void);
Value *val_new_affine(void); /* identity */
Value *val_new_volume4d_from_buf(int nx, int ny, int nz, int nt, double dx,
                                 double dy, double dz, double tr,
                                 const float *data);
Value *val_new_volume3d_from_buf(int nx, int ny, int nz, double dx, double dy,
                                 double dz, const float *data);
void val_free(Value *v);
Value *val_view_slice(Value *parent, int axis, int coord);
Value *val_view_tslice(Value *parent, int t);
Value *val_copy(const Value *v);
Value *val_load_nifti(const char *path);
int val_save_nifti(const Value *v, const char *path);
void val_print(const Value *v);
int64_t val_voxel_count(const Value *v);

#endif
