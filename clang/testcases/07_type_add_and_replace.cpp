//#include <stdio.h>
void printf(const char *x) {} void printf(const char *x, unsigned long y, char a, char b) {}

struct Particle {
  #pragma dastgen compressed
  bool x, y;
};

void printParticle(Particle p) {
  printf("Particle size in memory (bytes): %lu | Particle(x=%d, y=%d)\n", sizeof p, p.x, p.y);
}

bool testParticle(Particle &p) {
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      p.x = i;
      p.y = j;
      if (p.x != i || p.y != j) {
        printf("Test failed!\n");
        printParticle(p);
        return false;
      }
    }
  }
  // note that this test leaves the particle in the (true, true) state
  return true;
}

void printParticle(Particle *p) {
  printf("Particle size in memory (bytes): %lu | Particle(x=%d, y=%d)\n", sizeof *p, p->x, p->y);
}

void printParticle(Particle **p) {
  printf("Particle size in memory (bytes): %lu | Particle(x=%d, y=%d)\n", sizeof *(*p), (*p)->x, (*p)->y);
}

Particle createParticle() { return Particle(); }

int main() {
  Particle p1 {true, false};
  Particle p2 { .y = true, .x = false};
  Particle p4 = Particle(p2);
  Particle p5 = createParticle();
  printParticle(p1);
  p1.x = false;
  p1.y = !p1.x;
  printParticle(p1);
  testParticle(p2);
  printParticle(p2);
  p2 = p1;
  printParticle(p2);
  printParticle(&p2);
  Particle &p3 = p2;
  printParticle(&p3);
}
