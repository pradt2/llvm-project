#include <vector>

template<typename T, int size>
struct Vector {
  T data[size];

  Vector operator +(T v) {
    Vector copy = *this;
    for (int i = 0; i < size; i++) copy.data[i] += v;
    return copy;
  }

  Vector operator +(Vector v) {
    Vector copy = *this;
    for (int i = 0; i < size; i++) copy.data[i] += v.data[i];
    return copy;
  }
};

class Particle {
  using Vec = Vector<double, 2>;

  Vec pos;
  Vec vel;
  double rho;

public:
  Vec getPos() const {
    return pos;
  }

  void setPos(const Vec &v) {
    pos = v;
  }

  Vec getVel() const {
    return vel;
  }

  void setVel(const Vec &v) {
    vel = v;
  }

  double getRho() const {
    return rho;
  }

  void setRho(double v) {
    rho = v;
  }
};

template<typename P>
void mockLinearKernel(P &particle) {
  particle.setVel(particle.getVel() + particle.getRho());
}

void mockLinearKernelForLoop(auto *particles, int size) {
  [[clang::soa_conversion_target("particles")]]
  for (int i = 0; i < size; i++) {
    mockLinearKernel(particles[i]);
  }
}

void mockLinearKernelForRangeLoop(auto &particles) {
  [[clang::soa_conversion]]
  for (auto &particle : particles) {
    mockLinearKernel(particle);
  }
}

void mockQuadraticKernel(auto &pLocal, auto &pActive) {
  pLocal.setVel(pLocal.getVel() + pLocal.getRho() + pActive.getVel());
}

void mockQuadraticKernelForLoop(auto *pLocals, int pLocalsSize, auto *pActives, int pActivesSize) {
  [[clang::soa_conversion_target("pLocals")]]
  for (int i = 0; i < pLocalsSize; i++) {
    [[clang::soa_conversion_target("pActives")]]
    [[clang::soa_conversion_data_movement_strategy(move_to_outermost)]]
    for (int j = 0; j < pActivesSize; j++) {
      mockQuadraticKernel(pLocals[i], pActives[j]);
    }
  }
}

void mockQuadraticKernelForRangeLoop(auto &pLocals, auto &pActives) {
  [[clang::soa_conversion]]
  for (auto &pLocal : pLocals) {
    [[clang::soa_conversion]]
    [[clang::soa_conversion_data_movement_strategy(move_to_outermost)]]
    for (auto &pActive : pActives) {
      mockQuadraticKernel(pLocal, pActive);
    }
  }
}

int main() {
  mockLinearKernelForLoop((Particle*) 0, 1024);
  mockQuadraticKernelForLoop((Particle*) 0, 1024, (Particle*) 0, 1024);

  mockLinearKernelForRangeLoop(*(std::vector<Particle>*) 0);
  mockQuadraticKernelForRangeLoop(*(std::vector<Particle>*) 0, *(std::vector<Particle>*) 0);
}
