#include "code.h"
#include <stdio.h>

int main()
{
  fp_float fp = int_to_fp(10);
  printf("10 i -> %i f\n", fp);
  printf("%i f -> %i i\n", fp, fp_to_int(fp));
  printf("10*2 i -> %i i\n", fp_to_int(fp_mul_int(int_to_fp(10), 2)));
  printf("10/2 i -> %i i\n", fp_to_int(fp_div_int(int_to_fp(10), 2)));
  printf("10+2 i -> %i i\n", fp_to_int(fp_add_int(int_to_fp(10), 2)));
  printf("10-2 i -> %i i\n", fp_to_int(fp_sub_int(int_to_fp(10), 2)));
  printf("10*5 i -> %i i\n", fp_to_int(fp_mul(int_to_fp(10), int_to_fp(5))));
  printf("10/5 i -> %i i\n", fp_to_int(fp_div(int_to_fp(10), int_to_fp(5))));
  printf("10+5 i -> %i i\n", fp_to_int(fp_add(int_to_fp(10), int_to_fp(5))));
  printf("10-5 i -> %i i\n", fp_to_int(fp_sub(int_to_fp(10), int_to_fp(5))));
  return 0;
}