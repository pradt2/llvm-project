#include <stdio.h>
//void printf(const char *x) {} void printf(const char *x, unsigned long y, char a, char b, char c) {}

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

struct ParticleHolder {
  Particle p;
};

void printParticle(Particle p) {
  printf("Particle size in memory (bytes): %3lu | Particle(x=%4d, y=%4d, z=%4d)\n", sizeof p, p.x, p.y, p.z);
}

bool testParticle(Particle &p) {
  for (int i = 0; i < PARTICLE_RANGE; i++) {
    for (int j = 0; j < PARTICLE_RANGE; j++) {
      for (int k = ENUM_MIN_VAL; k <= ENUM_MAX_VAL; k++) {
        p.x = i;
        p.y = j;
        p.z = (X) k;
        if (p.x != i || p.y != j || p.z != k) {
          printf("Test failed! (target size %lu | vals x=%d, y=%d, z=%d)\n", sizeof(p), i, j, p.z);
          printParticle(p);
          return false;
        }
      }
    }
  }
  // note that this test leaves the particle in the (true, true) state
  return true;
}

void printParticle(Particle *p) {
  printf("Particle size in memory (bytes): %3lu | Particle(x=%4d, y=%4d, z=%4d)\n", sizeof *p, p->x, p->y, p->z);
}

void printParticle(Particle **p) {
  printf("Particle size in memory (bytes): %lu | Particle(x=%d, y=%d, z=%d)\n", sizeof *(*p), (*p)->x, (*p)->y, (*p)->z);
}

Particle createParticle() { return Particle(); }

int main() {
  Particle p1 {1, 0, X::a};
  printParticle(p1);
  Particle p2 { .y = 1, .x = 0, .z = X::a};
  printParticle(p2);
  Particle p4 = Particle(p2);
  Particle p5 = createParticle();
  Particle *p6 = new Particle();
  p1.y = !p1.y;
  p1.x = 20;
  p1.z = X::a;
  printParticle(p1);
  if (!testParticle(p2)) return 1;
  printParticle(p2);
  p2 = p1;
  printParticle(&p2);
  Particle &p3 = p2;
  printParticle(&p3);
  ParticleHolder ph {p3};
  printParticle(ph.p);
}
