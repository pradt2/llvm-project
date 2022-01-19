#include <stdio.h>

struct ParticleA {
  [[clang::compress_range(0, 16)]]
  char particleASpeed;
  [[clang::compress_range(0, 16)]]
  char particleAAge;

  int id;
};

struct ParticleB {
  short id;

  [[clang::compress_range(0, 16)]]
  char particleBSpeed;
  [[clang::compress_range(0, 16)]]
  char particleBAge;
};


void printParticle(ParticleA &p) { printf("Particle A(size=%lu, speed=%d, age=%d, id=%d)\n", sizeof p, p.particleASpeed, p.particleAAge, p.id); }
void printParticle(ParticleB &p) { printf("Particle B(size=%lu, speed=%d, age=%d, id=%d)\n", sizeof p, p.particleBSpeed, p.particleBAge, p.id); }

int main() {
  ParticleA a { .particleASpeed = 10, .particleAAge = 11, .id = 111 };
  printParticle(a);
  ParticleB b { .particleBSpeed = 14, .particleBAge = 15, .id = 115 };
  printParticle(b);
  a.particleASpeed = b.particleBSpeed;
  b.particleBAge = a.particleAAge;
  a.id = 77;
  b.id = 77;
  printParticle(a);
  printParticle(b);
}
