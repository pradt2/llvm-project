//#include <stdio.h>

struct B {
  [[clang::pack]]
  bool a;
};

template<class T, int X>
struct S {

  void setT(T val) {}
};

int main() {
  S<B, 1> s;
  S<B, 2> g;

//  printf("Bool value: %d", s.getA());
}
