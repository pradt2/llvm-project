#include <vector>

struct X {

  [[clang::pack]]
  bool a;

};

void method() {
  std::vector<X>::iterator myIterator;
}
