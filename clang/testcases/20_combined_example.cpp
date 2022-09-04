//#include <cassert>
#include <stdio.h>
#include <stdlib.h>

struct X1 {
  [[clang::pack]]
  bool a, b, c, d, e, f;
};

void myassert(bool result, const char * monit) {
  if (result) return ;
  printf("%s\n", monit);
  exit(1);
}

int main1() {
  X1 a;
  a.a = !true;
  a.b = !false;
  a.c = !true;
  a.d = !false;
  a.e = !true;
  a.f = !false;

  myassert(a.a != true, "a");
  myassert(a.b != false, "b");
  myassert(a.c != true, "c");
  myassert(a.d != false, "d");
  myassert(a.e != true, "e");
  myassert(a.f != false, "f");

  return 0;
}

struct X2 {
  [[clang::pack]]
  bool a;

  [[clang::truncate_mantissa(23)]]
  float flo;

  [[clang::pack]]
  bool b;

  [[clang::truncate_mantissa(52)]]
  double dou;
};

int main2() {
  X2 a;
  float flo = 0.1;
  double dou = 0.2;

  a.flo = flo;
  a.dou = dou;

  myassert(a.flo == flo, "flo");
  myassert(a.dou == dou, "dou");

  return 0;
}

struct X3 {


  [[clang::truncate_mantissa(52)]]
  double dou[7];

  [[clang::pack]]
  bool a[2];
};

int main() {
  X3 a;

  a.a[0] = true;
  a.a[1] = false;

  a.dou[0] = 0.2;
  a.dou[1] = 0.1;
  a.dou[2] = 0.2;
  a.dou[3] = 0.1;
  a.dou[4] = 0.2;
  a.dou[5] = 0.1;
  a.dou[6] = 0.2;

  myassert(a.a[0] == true, "one");
  myassert(a.a[1] == false, "two");

  myassert(a.dou[0] == 0.2, "d0");
  myassert(a.dou[1] == 0.1, "d1");
  myassert(a.dou[2] == 0.2, "d2");
  myassert(a.dou[3] == 0.1, "d3");
  myassert(a.dou[4] == 0.2, "d4");
  myassert(a.dou[5] == 0.1, "d5");
  myassert(a.dou[6] == 0.2, "d6");

  return 0;
}