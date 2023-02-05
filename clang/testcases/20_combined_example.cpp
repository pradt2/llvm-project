#include <unordered_set>
#include <cmath>
#include <iostream>

struct Particle {
  float x, y;
  float ax = 0, ay = 0;
  float vx = 0, vy = 0;

  float getX() { return x; }
  float getY() { return y; }

  float getAx() { return ax; }
  float getAy() { return ay; }
  void setAx(float val) { ax = val; }
  void setAy(float val) { ay = val; }
};


int main() {
  auto p1 = Particle {
    -1, 1
  };

  auto p2 = Particle {
      1, 1
  };

  auto p3 = Particle {
      -1, -1
  };

  auto p4 = Particle {
      1, -1
  };

  auto localParticles = std::unordered_set<Particle*> { &p1, &p2, &p3, &p4 };
  auto activeParticles = std::unordered_set<Particle*> { &p1, &p2, &p3, &p4 };


  [[clang::soa_conversion_target_size(localParticles.size())]]
  [[clang::soa_conversion_data_item("getX()", "")]]
  [[clang::soa_conversion_data_item("getY()", "")]]
  [[clang::soa_conversion_data_item("getAx()", "setAx()")]]
  [[clang::soa_conversion_data_item("getAy()", "setAy()")]]
  for (auto *l : localParticles) {

    [[clang::soa_conversion_target_size(activeParticles.size())]]
    [[clang::soa_conversion_data_item("getX()", "")]]
    [[clang::soa_conversion_data_item("getY()", "")]]
    [[clang::soa_conversion_data_movement_strategy(move_to_outermost)]]
    for (auto *a : activeParticles) {

        auto dx = a->getX() - l->getX();

        auto dy = a->getY() - l->getY();

        if (dx == 0 & dy == 0) continue ;

        auto ax = dx * 0.001;
        auto ay = dy * 0.001;

        l->setAx(l->getAx() + ax);
        l->setAy(l->getAy() + ay);
      }
  }

  for (auto *l : localParticles) {
      printf("Particle (x=%f, y=%f, ax=%f, ay=%f)\n", l->x, l->y, l->ax, l->ay);
  }

}
