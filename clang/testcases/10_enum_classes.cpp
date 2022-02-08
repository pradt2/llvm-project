enum class X {A = 0, B = 1};

#include <stdio.h>

struct S {
  [[clang::compress]]
  X a = X::A;
  [[clang::compress]]
  X b = X::B;
};


struct [[clang::compression_method(bitpack)]] Sb {
  [[clang::compress]]
  X a = X::A;
  [[clang::compress]]
  X b = X::B;
};

struct Sc {
  X a : 1;
  X b : 1;
};

int main() {
  S s = {};
  s.b = X::B;
  printf("A=%d, B=%d, a=%d, b=%d\n", X::A, X::B, s.a, s.b);
}
