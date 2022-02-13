#include <stdio.h>

struct Child {
  [[clang::compress]]
  bool a;
};

struct Parent {
  // alignment 4
  int a;
  // size should be 8 due to alignment, 5 if opt is applied
  Child c;
};

int main() {
  printf("Size %lu\n", sizeof(Parent));
}
