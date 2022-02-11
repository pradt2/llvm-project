enum class X {A = 0, B = 1};

//#include <stdio.h>

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
  X a : 2; //TODO assign bitpacked structs at min 2 bits
  X b : 2;
};

int main() {
  Sc s = {};
  s.b = X::B;
//  printf("A=%d, B=%d, a=%d, b=%d\n", X::A, X::B, s.a, s.b);
}
