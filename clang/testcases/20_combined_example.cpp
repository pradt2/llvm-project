//#include <stdio.h>

struct B {
  [[clang::pack]]
  bool a;
};

template<class T>
struct S {

  void setT(T val) {}
};

int main() {
  S<B> s;

//  printf("Bool value: %d", s.getA());
}
