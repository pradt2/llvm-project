//#include <functional>
#include <unordered_set>

namespace swift2 { namespace kernels {
template <typename ParticleXX, typename ParticleContainer>
void genericInteraction(
    ParticleContainer &localParticles,
    ParticleContainer &activeParticles);
} }

class Particle {
  [[clang::pack]] bool a;
};

template <typename ParticleXX, typename ParticleContainer>
void swift2::kernels::genericInteraction(
    ParticleContainer&  localParticles,
    ParticleContainer&  activeParticles
) {
  for (auto localParticle: localParticles ) {
      for (auto activeParticle: activeParticles ) {

      }
  }
}



int main() {
  std::unordered_set<Particle*> localParticles;
  std::unordered_set<Particle*> activeParticles;

  swift2::kernels::genericInteraction<Particle>(
      localParticles, activeParticles
      );

}
