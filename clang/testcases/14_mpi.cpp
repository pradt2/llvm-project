struct Data {

  struct {
    struct {
      int c;
    } b;
  } a;

  long xx;

  struct {
    struct {
      float f;
    } e;
  } d ;

  int i, g, h;

  [[clang::map_mpi_datatype]]
  static void* getMpiDataType();

  [[clang::map_mpi_datatype(a, d.e, g, h)]]
  static void *getMpiDataType2();
};
