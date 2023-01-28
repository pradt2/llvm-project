#include <vector>

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

  auto vector = std::vector<Struct*>();

  [[clang::soa_conversion_target_size(vector.size())]]
  [[clang::soa_conversion_data_item("getA()", "")]]
  [[clang::soa_conversion_data_item("getB()", "setB()")]]
  for (auto item : vector) {
    item->setB(item->getA() | item->getB());
  }

}
