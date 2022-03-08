//#include <stdio.h>
void printf(const char* s, int i, int b, int c);

struct S {

  [[clang::compress]]
  bool aTable[4][4];

  [[clang::compress]]
  bool x;

};

int main() {
  S s = {};
  s.aTable[1][2] = true;
  S s2 = {};
  s2.aTable[1][3] = s.aTable[1][2];
  s2.x = s.x;
  bool x = s.aTable[1][2];
  printf("Size %lu, val1: %d, val2: %d\n", sizeof(s), x, s.aTable[1][2]);
}
