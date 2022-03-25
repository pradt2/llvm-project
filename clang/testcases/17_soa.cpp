#include <stdio.h>
#include <vector>

//void printf(const char *s, int i, double b) {}

struct ParticleVelocity {
  double vx, vy, vz;
};

struct Particle {
  float x, y, z;
  ParticleVelocity *velocity;
};

#define SIZE 1 << 28

//int main() {
//  std::vector<Particle> particles = std::vector<Particle>(SIZE);
//
//  for (int i = 0; i < SIZE; i++) {
//    particles[i].x = 1 + i;
//    particles[i].y = 2 + i;
//    particles[i].z = 3 + i;
//    particles[i].velocity = new ParticleVelocity;
//    particles[i].velocity->vx = i + 3;
//    particles[i].velocity->vy = i + 3;
//    particles[i].velocity->vz = i + 3;
//  }
//
//  [[
//      clang::soa_conversion("x, velocity.vx", "x"),
//      clang::soa_conversion_target(particles),
//      clang::soa_conversion_target_size(particles.size())
//  ]]
//  for (int i = 0; i < SIZE; i++) {
//    particles[i].x += particles[i].velocity->vx * 0.01;
//  }
//
//  for (int i = 0; i < SIZE; i++) {
//    printf("Particle #%2d = (x=%4.6f)\n", i, particles[i].x);
//  }
//}


int main() {
    std::vector<Particle*> particles(SIZE);

    for (int i = 0; i < SIZE; i++) {
      particles[i] = new Particle;
      particles[i]->x = 1 + i;
      particles[i]->y = 2 + i;
      particles[i]->z = 3 + i;
      particles[i]->velocity = new ParticleVelocity;
      particles[i]->velocity->vx = i + 3;
      particles[i]->velocity->vy = i + 3;
      particles[i]->velocity->vz = i + 3;
    }

    [[
        clang::soa_conversion("x, velocity.vx", "x"),
        clang::soa_conversion_target_size(particles.size())
    ]]
    for (auto particle : particles) {
      for (int j = 0; j < 4; j++) {
        particle->x += particle->velocity->vx * 0.01;
      }
    }

    //for (int i = 0; i < SIZE; i++) {
    //  printf("Particle #%2d = (x=%4.6f)\n", i, particles[i].x);
    //}
}
