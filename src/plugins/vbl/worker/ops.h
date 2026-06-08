#ifndef VBL_OPS_H
#define VBL_OPS_H

#include "types.h"

typedef Value *(*OpFunc)(int argc, Value **args);

typedef struct {
  const char *name;
  OpFunc fn;
  int min_args;
  int max_args;
} OpDef;

OpDef *op_find(const char *name);

/* Built-in operations */
Value *op_mean(int argc, Value **args);
Value *op_tmean(int argc, Value **args);
Value *op_add(int argc, Value **args);
Value *op_sub(int argc, Value **args);
Value *op_mul(int argc, Value **args);
Value *op_div(int argc, Value **args);
Value *op_seed(int argc, Value **args);
Value *op_correlate(int argc, Value **args);
Value *op_threshold(int argc, Value **args);
Value *op_mask(int argc, Value **args);
Value *op_mask_int(int argc, Value **args);
Value *op_stdev(int argc, Value **args);
Value *op_min(int argc, Value **args);
Value *op_max(int argc, Value **args);
Value *op_sum(int argc, Value **args);
Value *op_noise(int argc, Value **args);
Value *op_vol3d(int argc, Value **args);
Value *op_vol4d(int argc, Value **args);
Value *op_smooth(int argc, Value **args);
Value *op_crop(int argc, Value **args);
Value *op_pad(int argc, Value **args);
Value *op_bandpass(int argc, Value **args);
Value *op_detrend(int argc, Value **args);
Value *op_tstd(int argc, Value **args);
Value *op_eq(int argc, Value **args);
Value *op_gt(int argc, Value **args);
Value *op_lt(int argc, Value **args);
Value *op_and(int argc, Value **args);
Value *op_or(int argc, Value **args);
Value *op_bcadd(int argc, Value **args);
Value *op_bcsub(int argc, Value **args);
Value *op_bcmul(int argc, Value **args);
Value *op_bcdiv(int argc, Value **args);
Value *op_slice(int argc, Value **args);
Value *op_tslice(int argc, Value **args);
Value *op_ts_range(int argc, Value **args);
Value *op_copy(int argc, Value **args);
Value *op_voxel(int argc, Value **args);
Value *op_affine(int argc, Value **args);
Value *op_translate(int argc, Value **args);
Value *op_rotate(int argc, Value **args);
Value *op_scale(int argc, Value **args);
Value *op_apply(int argc, Value **args);

#endif
