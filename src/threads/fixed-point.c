#ifndef THREADS_FIXED_POINT_C
#define THREADS_FIXED_POINT_C

#include "threads/fixed-point.h"

/*
 --------------------------------------------------------------------
 Description: turns an int into a fp_float
 --------------------------------------------------------------------
 */
fp_float
int_to_fp(int32_t i)
{
  return i * (1 << 14);
}

/*
 --------------------------------------------------------------------
 Description: turns an fp_float into an int
 --------------------------------------------------------------------
 */
int32_t
fp_to_int(fp_float f)
{
  if(f > 0)
  {
    return (f + (1 << 13)) / (1 << 14);
  }
  else if(f < 0)
  {
    return (f - (1 << 13)) / (1 << 14);
  }
  else
  {
    return 0;
  }
}

/*
 --------------------------------------------------------------------
 Description: adds two fp_floats together, result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_add(fp_float a, fp_float b)
{
  return a + b;
}

/*
 --------------------------------------------------------------------
 Description: subtracts two fp_floats. Result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_sub(fp_float a, fp_float b)
{
  return a - b;
}

/*
 --------------------------------------------------------------------
 Description: adds an fp_float and an int. Result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_add_int(fp_float a, int32_t i)
{
  return fp_add(a, int_to_fp(i));
}

/*
 --------------------------------------------------------------------
 Description: subtracts an fp_float and an int. Result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_sub_int(fp_float a, int32_t i)
{
  return fp_sub(a, int_to_fp(i));
}

/*
 --------------------------------------------------------------------
 Description: multiplies an fp_float and an fp_float. Result is an
    fp_float.
 --------------------------------------------------------------------
 */
fp_float
fp_mul(fp_float a, fp_float b)
{
  return ((int64_t) a) * b / (1 << 14);
}

/*
 --------------------------------------------------------------------
 Description: divides an fp_float and an fp_float. Returns an
    fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_div(fp_float a, fp_float b)
{
  return ((int64_t) a) * (1 << 14) / b;
}

/*
 --------------------------------------------------------------------
 Description: multiplies an fp_float and an int. Result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_mul_int(fp_float a, int32_t i)
{
  return a * i;
}

/*
 --------------------------------------------------------------------
 Description: divides an fp_float and an int. Result is an fp_float
 --------------------------------------------------------------------
 */
fp_float
fp_div_int(fp_float a, int32_t i)
{
  return a / i;
}

#endif /* threads/fixed-point.c */
