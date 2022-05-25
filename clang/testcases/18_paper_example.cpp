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
  float convergenceResidual = 1.0f;

  double energy;
};

int main() {
  Particle p;
  p.moveState = MoveState::B;
  printf("moveState = %d\n", p.moveState);
  p.iterationCount = 128;
  printf("iterationCount = %d\n", p.iterationCount);
  p.convergenceResidual = 1.125f;
  printf("convergenceResidual = %f\n", p.convergenceResidual);
  p.energy = 2e5f;
  printf("energy %f", p.energy);
}