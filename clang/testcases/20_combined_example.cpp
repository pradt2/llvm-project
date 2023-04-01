#include <mpi.h>
#include <cstddef>
#include "stdio.h"
#include <chrono>

struct Particle {
  [[clang::pack]]
  bool a, b;

  [[clang::map_mpi_datatype()]]
  static MPI_Datatype getMpiDatatype();
};

#define DATA_SIZE (2 << 29)

int main() {
  MPI_Init(NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  // Print off a hello world message
//  printf("Hello world from rank %d out of %d processors\n", world_rank, world_size);

  Particle *particles = new Particle[DATA_SIZE];

  MPI_Request request;

  if (world_rank == 0) {

//    for (unsigned long i = 0; i < DATA_SIZE; i++) {
//      particles[i].a = i % 2 == 0;
//      particles[i].b = i % 3 == 0;
//    }

    printf("Sending...");
    auto start = std::chrono::high_resolution_clock::now();
    MPI_Isend( particles,    DATA_SIZE, Particle::getMpiDatatype(), 1, 0, MPI_COMM_WORLD, &request );
    std::chrono::duration<double> time = std::chrono::high_resolution_clock::now() - start;
    printf("Sent in %f!\n", time.count());
    MPI_Wait( &request, MPI_STATUS_IGNORE );
  } else {
    printf("Receiving...");
    auto start = std::chrono::high_resolution_clock::now();
    MPI_Recv(  particles, DATA_SIZE, Particle::getMpiDatatype(), 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );
    std::chrono::duration<double> time = std::chrono::high_resolution_clock::now() - start;
    printf("Received in %f!\n", time.count());

//    MPI_Wait( &request, MPI_STATUS_IGNORE );

//    for (unsigned long i = 0; i < DATA_SIZE; i++) {
//      if (particles[i].a != i % 2 == 0) {
//        printf("FaILED!\n");
//        exit(1);
//      }
//      if (particles[i].b != i % 3 == 0) {
//        printf("FaILED!\n");
//        exit(1);
//      }
//    }
  }

  // Finalize the MPI environment.
  MPI_Finalize();
}
