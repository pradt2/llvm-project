struct DataA {
  [[clang::compress]]
  bool a;
  static void staticDataA(DataA &a) {}
};

int main() {
  DataA a = {};
  DataA::staticDataA(a);
}
