void printf(const char* s, int i, int b);
#define ARR_SIZE 2

struct S {

  [[clang::compress]]
  bool a[ARR_SIZE - 1][ARR_SIZE][ARR_SIZE + 1];

  [[clang::compress]]
  bool x;

};

int main() {
  S s = {};
  int idx = 0;
  s.a[idx][idx][idx] = true;
  printf("Size %lu, val: %d", sizeof(S), s.a[idx][idx][idx]);
}
