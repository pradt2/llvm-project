#include <stdio.h>

int main() {


#ifdef __PACKED_ATTRIBUTES_LANGUAGE_EXTENSION__
  printf("PACKED\n");
#endif


#ifdef __MPI_ATTRIBUTES_LANGUAGE_EXTENSION__
  printf("MPI\n");
#endif

#ifdef __SOA_CONVERSION_ATTRIBUTES_EXTENSION__
  printf("SOA\n");
#endif

}