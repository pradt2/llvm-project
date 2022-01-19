#include <stdio.h>

struct ParticleA {
  [[clang::compress_range(0, 16)]]
  char particleASpeed;
  [[clang::compress_range(0, 16)]]
  char particleAAge;
};

struct ParticleB {
  [[clang::compress_range(0, 16)]]
  char particleBSpeed;
  [[clang::compress_range(0, 16)]]
  char particleBAge;
};


void printParticle(ParticleA &p) { printf("Particle A(size=%lu, speed=%d, age=%d)\n", sizeof p, p.particleASpeed, p.particleAAge); }
void printParticle(ParticleB &p) { printf("Particle B(size=%lu, speed=%d, age=%d)\n", sizeof p, p.particleBSpeed, p.particleBAge); }

int main() {
  ParticleA a { .particleASpeed = 10, .particleAAge = 11 };
  printParticle(a);
  ParticleB b { .particleBSpeed = 14, .particleBAge = 15 };
  printParticle(b);
  a.particleASpeed = b.particleBSpeed;
  b.particleBAge = a.particleAAge;
  printParticle(a);
  printParticle(b);
}
