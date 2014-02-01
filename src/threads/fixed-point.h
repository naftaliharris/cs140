#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

typedef int32_t fp_float;
fp_float int_to_fp (int32_t i);
int32_t fp_to_int (fp_float f);
fp_float fp_add (fp_float a, fp_float b);
fp_float fp_sub (fp_float a, fp_float b);
fp_float fp_add_int (fp_float a, int32_t i);
fp_float fp_sub_int (fp_float a, int32_t i);
fp_float fp_mul (fp_float a, fp_float b);
fp_float fp_div (fp_float a, fp_float b);
fp_float fp_mul_int (fp_float a, int32_t i);
fp_float fp_div_int (fp_float a, int32_t i);

#endif /* threads/fixed-point.h */
