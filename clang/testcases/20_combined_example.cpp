struct Struct {
  bool a;

  bool getA() { return a; }

  void setA(bool val) {
    a = val;
  }
};

int main() {

  Struct *aTable = nullptr;

  int size = 10;

  [[clang::soa_conversion_target(aTable)]]
  [[clang::soa_conversion_target_size(size)]]
  [[clang::soa_conversion_inputs("getA()")]]
  [[clang::soa_conversion_outputs("setA($val)")]]
  for (int i = 0; i < size; i++) {
    aTable[i].setA( ! aTable->getA() );
  }

}
