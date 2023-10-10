#include <stdio.h>
#include <functional>
#include <mpi.h>

struct Motorcycle {
  int engineSize;
  int topSpeed;

  [[clang::map_mpi_datatype]]
  static MPI_Datatype getMyMpiMapping();
};

struct Car {
  [[clang::compress_range(255)]]
  int shifts;
  [[clang::compress_range(255)]]
  int topSpeed;

  [[clang::map_mpi_datatype("shifts")]]
  static MPI_Datatype getMpi();
};

struct CarWrapper {
  Car car;

  [[clang::map_mpi_datatype]]
  static MPI_Datatype getMpi();
};

int main(int argc, char** argv) {

  return 0;
}
