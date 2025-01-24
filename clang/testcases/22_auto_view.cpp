//#include <vector>

namespace ns {
struct DataA {
  double a, b;
};

struct DataB {
  int a, b;
};

void kernel(auto &data) { data.a += data.b; }

void kernel_launch(auto *data, int size);

}

void ns::kernel_launch(auto *data, int size) {
  [[clang::soa_conversion_target("data")]]
  for (int i = 0; i < size; i++) {
    ns::kernel(data[i]);
  }
}

int main() {
  ns::kernel_launch((ns::DataA*) 1, 1);
  ns::kernel_launch((ns::DataB*) 1, 1);
}
