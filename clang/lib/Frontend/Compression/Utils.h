//
// Created by p on 04/09/22.
//

#ifndef CLANG_UTILS_H
#define CLANG_UTILS_H

#include <string>
#include <limits>

std::string to_constant(unsigned long a) {
  std::string constant = std::to_string(a) + "U";
  if (a > std::numeric_limits<unsigned int>().max()) constant += "L";
  return constant;
}

std::string to_constant(long a) {
  std::string constant = std::to_string(a);
  if (a > std::numeric_limits<int>().max()) constant += "L";
  return constant;
}

std::string to_constant(unsigned int a) {
  return to_constant((unsigned long) a);
}

std::string to_constant(unsigned short a) {
  return to_constant((unsigned long) a);
}

std::string to_constant(unsigned char a) {
  return to_constant((unsigned long) a);
}

std::string to_constant(int a) {
  return to_constant((long) a);
}

std::string to_constant(short a) {
  return to_constant((long) a);
}

std::string to_constant(char a) {
  return to_constant((long) a);
}

#endif // CLANG_UTILS_H
