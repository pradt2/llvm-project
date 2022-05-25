#include <stdio.h>

enum MoveState {
  A, B, C
};

struct Particle {
  [[clang::pack]]
  MoveState moveState;

#ifdef ITERATION_LIMIT
  [[clang::pack_range(0, ITERATION_LIMIT)]]
#endif
  int iterationCount;

  [[clang::truncate_mantissa(7)]]
  float convergenceResidual = 1.0;

  double energy;
};

int main() {
  Particle p { A, 0 };
  printf("convergence = %f\n", p.convergenceResidual);
}