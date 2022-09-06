#include <stdio.h>
#include <stdlib.h>

//struct X1 {
//  [[clang::pack]]
//  bool a, b, c, d, e, f;
//};
//
void assert(bool result, const char * monit) {
  if (result) return ;
  printf("%s\n", monit);
  exit(1);
}
//
//int main1() {
//  X1 a;
//  a.a = !true;
//  a.b = !false;
//  a.c = !true;
//  a.d = !false;
//  a.e = !true;
//  a.f = !false;
//
//  assert(a.a != true, "a");
//  assert(a.b != false, "b");
//  assert(a.c != true, "c");
//  assert(a.d != false, "d");
//  assert(a.e != true, "e");
//  assert(a.f != false, "f");
//
//  return 0;
//}
//
//struct X2 {
//  [[clang::pack]]
//  bool a;
//
//  [[clang::truncate_mantissa(23)]]
//  float flo;
//
//  [[clang::pack]]
//  bool b;
//
//  [[clang::truncate_mantissa(52)]]
//  double dou;
//};
//
//int main2() {
//  X2 a;
//  float flo = 0.1;
//  double dou = 0.2;
//
//  a.flo = flo;
//  a.dou = dou;
//
//  assert(a.flo == flo, "flo");
//  assert(a.dou == dou, "dou");
//
//  return 0;
//}
//
//struct X3 {
//
//
//  [[clang::truncate_mantissa(52)]]
//  double dou[7];
//
//  [[clang::pack]]
//  bool a[2];
//};
//
//int main3() {
//  X3 a;
//
//  a.a[0] = true;
//  a.a[1] = false;
//
//  a.dou[0] = 0.2;
//  a.dou[1] = 0.1;
//  a.dou[2] = 0.2;
//  a.dou[3] = 0.1;
//  a.dou[4] = 0.2;
//  a.dou[5] = 0.1;
//  a.dou[6] = 0.2;
//
//  assert(a.a[0] == true, "one");
//  assert(a.a[1] == false, "two");
//
//  assert(a.dou[0] == 0.2, "d0");
//  assert(a.dou[1] == 0.1, "d1");
//  assert(a.dou[2] == 0.2, "d2");
//  assert(a.dou[3] == 0.1, "d3");
//  assert(a.dou[4] == 0.2, "d4");
//  assert(a.dou[5] == 0.1, "d5");
//  assert(a.dou[6] == 0.2, "d6");
//
//  return 0;
//}

//struct X4 {
//  [[clang::pack_range(0, 32768)]]
//  unsigned short a,b;
//};
//
//int main4() {
//  // infinite expansion in float/double form,but I don't know if the LSB is 1 or 0
//  long originalValue = 32767 / 2 / 2;
//
//  X4 packed1;
//  packed1.a = originalValue;
//  packed1.b = originalValue;
//
//  assert(packed1.a == packed1.b, "Packed.a != Packed.b");
//
//  long unpacked1A = packed1.a;
//
//  assert(packed1.a == unpacked1A, "Packed.a != unpacked1A");
//
//  long unpacked1B = packed1.b;
//
//  assert(unpacked1A == unpacked1B, "unpacked1A != unpacked1B");
//
//  assert(packed1.a == originalValue, "Packed.a != originalValue?");
//}

#ifndef SIZE
#define SIZE 4
#endif

//struct X6 {
//
//  bool uncomp1;
//
//  [[clang::pack_range(32767)]]
//  unsigned short shorts[12];
//
//  bool uncomp2;
//
//  [[clang::truncate_mantissa(SIZE)]]
//  double d[2];
//
//  [[clang::truncate_mantissa(25)]]
//  double e[4];
//};
//
//int main() {
//  double D = -0.1;
//
//  printf("SIZE = %d\n", SIZE);
//
//  X6 x;
//
//  x.d[0] = D;
//  x.d[1] = D;
//  printf("x.d[0] = %.20f , x.d[1] = %.20f\n", x.d[0], x.d[1]);
//
//  assert(x.d[0] == x.d[1], "x.d[0] != x.d[1]");
//
//  double upa = x.d[0];
//  assert(x.d[0] == upa, "x.d[0] != upa");
//
//  double upb = x.d[1];
//  assert(x.d[1] == upb, "x.d[1] != upb");
//
//  if (SIZE < 52) {
//    assert(upa != D, "upa == D");
//  }
//
//  printf("delta = %.20f\n", D - upa);
//}


struct X5 {

    [[clang::pack_range(32767)]]
    unsigned short shorts[1];

  [[clang::truncate_mantissa(SIZE)]]
  double a, b;
};

int main() {
  double D = 0.1;

  printf("SIZE = %d\n", SIZE);

  X5 x;

  x.a = D;
  x.b = D;
  printf("x.a = %.20f , x.b = %.20f\n", x.a, x.b);

  assert(x.a == x.b, "x.a != x.b");

  double upa = x.a;
  assert(x.a == upa, "x.a != upa");

  double upb = x.b;
  assert(x.b == upb, "x.b != upb");

  if (SIZE < 52) {
    assert(upa != D, "upa == D");
  }

  printf("delta = %.20f\n", D - upa);
  return 0;
}

