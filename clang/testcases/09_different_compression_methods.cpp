#include <stdio.h>

struct [[clang::compression_method(bitshift)]] BitshiftPackedStruct {
  [[clang::compress_range(240, 256)]]
  unsigned char a;
  [[clang::compress_range(240, 256)]]
  unsigned char b;
};


struct [[clang::compression_method(bitpack)]] BitpackPackedStruct {
  [[clang::compress_range(240, 256)]]
  unsigned char a;
  [[clang::compress_range(240, 256)]]
  unsigned char b;
};


int main() {
  BitshiftPackedStruct bitshiftObj {250, 240 };
  BitpackPackedStruct bitpackObj { 254, 244 };

  printf("BitshiftPackedStruct size in memory (bytes): %3lu | BitshiftPackedStruct(a=%d, b=%d)\n", sizeof bitshiftObj, bitshiftObj.a, bitshiftObj.b);
  printf("BitpackPackedStruct size in memory (bytes): %3lu | BitshiftPackedStruct(a=%d, b=%d)\n", sizeof bitpackObj, bitpackObj.a, bitpackObj.b);
}
