#include <stdio.h>

struct X {
  [[clang::pack]] bool a, b;
};

int main() {
}