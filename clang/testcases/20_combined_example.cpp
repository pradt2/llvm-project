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

//using DATACLASS = Struct;

#define TEMPLATED_MAIN(DATACLASS) { \
                                    DATACLASS *dataOuter = new DATACLASS[10]; \
                                    DATACLASS *dataInner = new DATACLASS[10]; \
 \
                                    [[clang::soa_conversion_target(dataOuter)]] \
                                    [[clang::soa_conversion_target_size(10)]] \
                                    [[clang::soa_conversion_data_item("getA()", "")]] \
                                    [[clang::soa_conversion_data_item("getB()", "setB()")]] \
                                    for (int i = 0; i < 10; i++) { \
 \
                                    [[clang::soa_conversion_target(dataInner)]] \
                                    [[clang::soa_conversion_target_size(10)]] \
                                    [[clang::soa_conversion_data_item("getB()", "")]] \
                                    [[clang::soa_conversion_data_movement_strategy(move_to_outermost)]] \
                                    for (int j = 0; j < 10; j++) { \
                                    dataOuter[i].setB(dataOuter[i].getA() | dataInner[j].getB()); \
                                    } \
 \
                                    } \
}
void templatedMain() {


}

int main() {

  TEMPLATED_MAIN(Struct)

}
