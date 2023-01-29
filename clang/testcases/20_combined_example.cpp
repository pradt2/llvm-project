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

  auto vectorOuter = std::vector<Struct*>();
  auto vectorInner = std::vector<Struct*>();

  [[clang::soa_conversion_target_size(vectorOuter.size())]]
  [[clang::soa_conversion_data_item("getA()", "")]]
  [[clang::soa_conversion_data_item("getB()", "setB()")]]
  for (auto itemOuter : vectorOuter) {

    [[clang::soa_conversion_target_size(vectorInner.size())]]
    [[clang::soa_conversion_data_item("getB()", "")]]
    [[clang::soa_conversion_data_movement_strategy(move_to_outermost)]]
    for (auto itemInner: vectorInner) {
      itemOuter->setB(itemOuter->getA() | itemInner->getB());
    }

  }

}
