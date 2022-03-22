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

  [[clang::map_mpi_datatype]]
  static MPI_Datatype getMpi();
};

int main(int argc, char** argv) {

  const int tag = 13;
  int size, rank;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size < 2) {
    fprintf(stderr,"Requires at least two processes.\n");
    exit(-1);
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    Car sendCar = {};
    sendCar.shifts = 5;
    sendCar.topSpeed = 240;
    MPI_Send(&sendCar, 1, Car::getMpi(), 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: TX: %d %d\n", rank, sendCar.shifts, sendCar.topSpeed);

    Motorcycle sendMotorcycle = {};
    sendMotorcycle.engineSize = 125;
    sendMotorcycle.topSpeed = 240;
    MPI_Send(&sendMotorcycle, 1, Motorcycle::getMyMpiMapping(), 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: TX: %d %d\n", rank, sendMotorcycle.engineSize, sendMotorcycle.topSpeed);
  }
  if (rank == 1) {

    Car recvCar = {};
    MPI_Recv(&recvCar, 1, Car::getMpi(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: RX: %d %d\n", rank, recvCar.shifts, recvCar.topSpeed);

    Motorcycle recvMotorcycle = {};
    MPI_Recv(&recvMotorcycle, 1, Motorcycle::getMyMpiMapping(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: RX: %d %d\n", rank, recvMotorcycle.engineSize, recvMotorcycle.topSpeed);
  }

  MPI_Finalize();

  return 0;
}
