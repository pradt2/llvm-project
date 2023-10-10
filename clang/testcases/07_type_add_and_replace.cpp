//#include <stdio.h>
void printf(const char *x) {} void printf(const char *x, unsigned long y, char a, char b, char c) {}

#define PARTICLE_RANGE 64
#define ENUM_MIN_VAL 1023
#define ENUM_MAX_VAL 1024

enum X {
  a = ENUM_MIN_VAL, b = ENUM_MAX_VAL
};

struct Particle {
  [[clang::compress_range(0, PARTICLE_RANGE)]]
  short x, y;
  [[clang::compress]]
  X z;
};

int main() {
  Particle *p6 = new Particle();
}
