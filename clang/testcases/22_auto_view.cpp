void doMe(auto *data) {
  auto v = data->a + data->b;
  data->c = v;
  data->d() += 1;
  data->arr[0] = data->arr[1];
}

void kernel(auto *data) {
  doMe(data);

//  escapeF(*data);
}

struct Data {
  double a, b, c, _d;

  int arr[2];

  void x() {}

  double &d() {
    x();
    return _d;
  }
};

void kernel_launcher(Data *data) {
  kernel(data);
}
