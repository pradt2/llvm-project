struct Struct {
  bool a;
  bool b;

  bool getA() { return a; }

  void setA(bool val) {
    a = val;
  }

  bool getB() { return b; }

  void setB(bool val) {
    b = val;
  }
};

int main() {

  Struct *aTable = nullptr;

  int size = 10;

  [[clang::soa_conversion_target(aTable)]]
  [[clang::soa_conversion_target_size(size)]]
  [[clang::soa_conversion_data_item("getA()", "")]]
  [[clang::soa_conversion_data_item("getB()", "setB()")]]
  for (int i = 0; i < size; i++) {
    aTable[i].setB(aTable[i].getA() | aTable[i].getB());
  }

}
