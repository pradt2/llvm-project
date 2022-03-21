#include <stdio.h>
#include <functional>
#include <mpi.h>

struct Car {
  [[clang::compress_range(255)]]
  int shifts;
  [[clang::compress_range(255)]]
  int topSpeed;

  static MPI_Datatype getMpiDatatype() {};
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
    Car send = {};
    send.shifts = 5;
    send.topSpeed = 240;
    MPI_Send(&send, 1, Car::getMpiDatatype(), 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: TX: %d %d\n", rank, send.shifts, send.topSpeed);
  }
  if (rank == 1) {

    Car recv = {};
    MPI_Recv(&recv, 1, Car::getMpiDatatype(), 0, tag, MPI_COMM_WORLD, nullptr);

    printf("Rank %d: RX: %d %d\n", rank, recv.shifts, recv.topSpeed);
  }

  MPI_Finalize();

  return 0;
}
