#include <mpi.h>

namespace myDifferentNamespace {
template<typename TYP>
struct TemplateData {
  TYP templateInboard;
};
}


namespace myNamespace {
  struct Data {
    [[clang::compress]]
    bool a;
    [[clang::compress]]
    bool b;
    myDifferentNamespace::TemplateData<float> templateData;
  };
}

int main() {
  myNamespace::Data data = {};
  data.a = true;
}
