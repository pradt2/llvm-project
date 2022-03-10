//#include <stdio.h>

enum type: unsigned long {
  audi, mercedes
};

struct Interior {
  bool isCool;
  int seats[2][2];
  type type[2];
};

struct car {
          [[clang::compress_range(255)]]
  int shifts;
  Interior interior[2];
          [[clang::compress_range(255)]]
  int topSpeed;
};

int main() {
  car send = {};
  send.shifts = 4;
  send.topSpeed = 100;
//  printf("%d %d\n", send.shifts, send.topSpeed);
}