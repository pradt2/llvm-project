struct SubData {
  double usedSubA, usedSubB;
  double unusedSubA, unusedSubB;
};

struct Data {
  double usedA, usedB;

  union {
    struct {
      double densityUsedA;
      double densityUnusedA;
    } density;

    struct {
      double forceUsedA;
      double forceUnusedA;

      double getForce() const {
        return this->forceUsedA;
      }

      void setForce(double f) {
        this->forceUsedA = f;
      }

    } force;
  };

  SubData usedSubdataA;

  SubData unusedSubdataA;

  using DensityKernelView [[clang::view("densityKernel")]] = Data;
  using ForceKernelView [[clang::view("forceKernel")]] = Data;
};

void densityKernel(Data *d1, Data *d2) {
  auto _1 = d1->usedA - d2->usedB;
  auto _2 = d1->usedSubdataA.usedSubA - d2->usedSubdataA.usedSubB;
  auto _3 = d1->density.densityUsedA - d2->density.densityUsedA;
  d1->density.densityUsedA = _1 * _2 * _3;
}

void forceKernel(Data *d1, Data *d2) {
  auto _1 = d1->usedA - d2->usedB;
  auto _2 = d1->usedSubdataA.usedSubA - d2->usedSubdataA.usedSubB;
  auto _3 = d1->force.getForce() - d2->force.getForce();
  d1->force.setForce(_1 * _2 * _3);
}
