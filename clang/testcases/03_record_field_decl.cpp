#include <stdio.h>

template<typename X>
X plus(X a, X b) {
  X result = a;
  result += b;
  return result;
}

int main() {
  printf("%d\n", plus(3, 2));
  printf("%f\n", plus(3.f, 2.f));
}
