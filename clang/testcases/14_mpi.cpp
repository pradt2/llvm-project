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

struct CarWrapper {
  Car car;

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

    printf("Rank %d: CAR(size=%lu) TX: %d %d\n", rank, sizeof(sendCar), sendCar.shifts, sendCar.topSpeed);

    CarWrapper sendCarWrapper = {sendCar};
    MPI_Send(&sendCar, 1, Car::getMpi(), 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: CWR(size=%lu) TX: %d %d\n", rank, sizeof(sendCarWrapper), sendCarWrapper.car.shifts, sendCarWrapper.car.topSpeed);

    Motorcycle sendMotorcycle = {};
    sendMotorcycle.engineSize = 125;
    sendMotorcycle.topSpeed = 240;
    MPI_Send(&sendMotorcycle, 1, Motorcycle::getMyMpiMapping(), 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: MTR(size=%lu) TX: %d %d\n", rank, sizeof(sendMotorcycle), sendMotorcycle.engineSize, sendMotorcycle.topSpeed);
  }
  if (rank == 1) {

    Car recvCar = {};
    MPI_Recv(&recvCar, 1, Car::getMpi(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: CAR(size=%lu) RX: %d %d\n", rank, sizeof(recvCar), recvCar.shifts, recvCar.topSpeed);

    CarWrapper recvCarWrapper = {};
    MPI_Recv(&recvCarWrapper, 1, CarWrapper::getMpi(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: CWR(size=%lu) RX: %d %d\n", rank, sizeof(recvCarWrapper), recvCarWrapper.car.shifts, recvCarWrapper.car.topSpeed);

    Motorcycle recvMotorcycle = {};
    MPI_Recv(&recvMotorcycle, 1, Motorcycle::getMyMpiMapping(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: MTR(size=%lu) RX: %d %d\n", rank, sizeof(recvMotorcycle), recvMotorcycle.engineSize, recvMotorcycle.topSpeed);
  }

  MPI_Finalize();

  return 0;
}
