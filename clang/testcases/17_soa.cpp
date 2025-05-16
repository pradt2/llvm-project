struct Data {
  int a, b, c, d;
};

void doWork(Data *data, int size) {
  [[clang::soa_conversion_target("data")]]
  for (int i = 0; i < size; i++) {
    data[i].a += data[i].b;
  }
}
