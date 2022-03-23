//#include <stdio.h>
//#include <vector>

void printf(const char *s, int a, double b, double c) {}

struct Particle {
  double a, b, c;
};

#define SIZE 2

void calculateParticles(Particle *particles) {
//  struct ParticleSOA {
//    double *a;
//    double *b;
//  } particleSoa;
//
//  particleSoa.a = new double[SIZE];
//  particleSoa.b = new double[SIZE];
//
//  for (int i = 0; i < SIZE; i++) {
//    particleSoa.a[i] = particles[i].a;
//    particleSoa.b[i] = particles[i].b;
//  }
//
//  for (int i = 0; i < SIZE; i++) {
////    particles[i].c = particles[i].a * particles[i].b;
//    particleSoa.a[i] = particleSoa.a[i] * particleSoa.b[i];
//  }
//
//  for (int i = 0; i < SIZE; i++) {
//    particles[i].a = particleSoa.a[i];
//  }
}

int main() {
    Particle *particles = new Particle[SIZE];
    for (int i = 0; i < SIZE; i++) {
      particles[i].a = 1 + i;
      particles[i].b = 2 + i;
    }

    calculateParticles(particles);

    [[
        clang::soa_conversion("a, b", "a"),
        clang::soa_conversion_target(particles),
        clang::soa_conversion_target_size(SIZE)
    ]]
    for (int i = 0; i < SIZE; i++) {
      printf("Particle #%2d = (a=%4.6f, b=%4.6f)\n", i, particles[i].a, particles[i].b);
    }

//    std::vector<int> vectorOfInts;
//    for (int i : vectorOfInts) {
//      printf("%d\n", i);
//    }
}
