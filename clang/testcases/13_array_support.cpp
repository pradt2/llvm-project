#include <stdio.h>
//void printf(const char* s, int i, int b);

struct S {

  [[clang::compress]]
  bool aTable[4][4];

};

int main() {
  S s = {};
  s.aTable[1][2] = true;
  printf("Size %lu, val: %d\n", sizeof(s), s.aTable[1][2]);
}
