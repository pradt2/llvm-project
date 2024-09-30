//#include <stdio.h>
void printf(const char*, float f, double d, int i) {}

struct S {
  [[clang::compress_truncate_mantissa(8)]]
  float f;
  [[clang::compress_truncate_mantissa(50)]]
  double d;
  [[clang::compress_range(255)]]
  int i;
  [[clang::compress_truncate_mantissa(50)]]
  double d_arr[4];
};

int main() {
  S s {0.875, 0.625, 100};
  printf("f=%f, d=%f , i=%d\n", s.f, s.d, s.i);
}
