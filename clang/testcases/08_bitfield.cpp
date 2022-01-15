#include <stdio.h>

struct StructA {
  bool a : 1 { true };
  bool b : 1;
};

struct StructB {
  unsigned short int0: 1 = 1, b : 2;
  unsigned char intA : 1;
  unsigned char intB : 1;
  bool boolA : 1;
};

int main() {
  printf("Value of a: %d\n", StructA().a);
  printf("Size StructA: %lu\n", sizeof(StructA));
  printf("Size StructB: %lu\n", sizeof(StructB));
}