#include <stdio.h>

struct StructA {
  bool a : 1 { true };
  bool b : 1;
};

enum X{a,b};
#pragma pack(push, 1)
struct StructB {
  unsigned short int0: 1 = 1, b : 2;
  unsigned char intA : 1;
  unsigned char intB : 1;
  bool boolA : 1;
  X a : 2;
};
#pragma pack(pop)

int main() {
  printf("Value of a: %d\n", StructA().a);
  printf("Size StructA: %lu\n", sizeof(StructA));
  printf("Size StructB: %lu\n", sizeof(StructB));
}