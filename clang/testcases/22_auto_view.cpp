#include <vector>

struct DataA {
  double a, b;
};

struct DataB {
  int a, b;
};

void kernel(auto &data) {
  data.a += data.b;
}

void kernel_launch(auto *data, int size) {
  [[clang::soa_conversion_target("data")]]
  for (int i = 0; i < size; i++) {
    kernel(data[i]);
  }
}

int main() {
  kernel_launch((DataA*) 1, 1);
  kernel_launch((DataB*) 1, 1);
}
