#include <stdio.h>
#include <functional>
#include <mpi.h>

struct car {
  [[clang::compress_range(255)]]
  int shifts;
  [[clang::compress_range(255)]]
  int topSpeed;

  int getSenderRank() const;
  static void send(const car &buffer, int destination, int tag, MPI_Comm communicator);
  static void receive(car &buffer, int source, int tag, MPI_Comm communicator );
  static void send(const car &buffer, int destination, int tag, std::function<void()> waitFunctor, MPI_Comm communicator );
  static void receive(car &buffer, int source, int tag, std::function<void()> waitFunctor, MPI_Comm communicator );
  static void shutdownDatatype();
  static void initDatatype();
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

  car::initDatatype();

  if (rank == 0) {
    car send = {};
    send.shifts = 5;
    send.topSpeed = 240;
    car::send(send, 1, tag, MPI_COMM_WORLD);

    printf("Rank %d: TX: %d %d\n", rank, send.shifts, send.topSpeed);
  }
  if (rank == 1) {

    car recv = {};
    car::receive(recv, 0, tag, MPI_COMM_WORLD);

    printf("Rank %d: TX: %d %d\n", rank, recv.shifts, recv.topSpeed);
  }

  car::shutdownDatatype();
  MPI_Finalize();

  return 0;
}
