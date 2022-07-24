#include <stdio.h>
#include <bitset>

#define TwoPowerD 256

struct S {
  [[clang::pack]] std::bitset<TwoPowerD> bitset;
  [[clang::pack]] bool b;
};

int main() {
  S s;
  s.bitset.flip(0);
  printf("Bool val: %d\n", s.bitset.test(0));
  printf("Size s: %lu\n", sizeof s);
}

//enum MoveState {
//  A, B, C
//};
//
//struct Particle {
//  [[clang::pack]]
//  MoveState moveState;
//
//#ifdef ITERATION_LIMIT
//  [[clang::pack_range(0, ITERATION_LIMIT)]]
//#endif
//  int iterationCount;
//
//  [[clang::truncate_mantissa(7)]]
//  float convergenceResidual = 1.0f;
//
//  double energy;
//};
//
//int main() {
//  Particle p;
//  p.moveState = MoveState::B;
//  printf("moveState = %d\n", p.moveState);
//  p.iterationCount = 128;
//  printf("iterationCount = %d\n", p.iterationCount);
//  p.convergenceResidual = 1.125f;
//  printf("convergenceResidual = %f\n", p.convergenceResidual);
//  p.energy = 2e5f;
//  printf("energy %f\n", p.energy);
//}

