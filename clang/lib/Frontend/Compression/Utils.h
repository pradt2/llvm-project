//
// Created by p on 04/09/22.
//

#ifndef CLANG_UTILS_H
#define CLANG_UTILS_H

#include <string>
#include <limits>

using namespace clang;

template<typename T>
static inline T *getParentNodeOfType(ASTContext &C, DynTypedNode node) {
  for (auto parent: C.getParentMapContext().getParents(node)) {
    auto *exprMaybe = parent.get<T>();
    if (exprMaybe) return (T*) exprMaybe;
    exprMaybe = getParentNodeOfType<T>(C, parent);
    if (exprMaybe) return (T*) exprMaybe;
  }
  return nullptr;
}

template<typename T, typename U>
static inline T *getParentNodeOfType(ASTContext &C, U *E) {
  auto dynNode = DynTypedNode::create(*E);
  auto *parentMaybe = getParentNodeOfType<T>(C, dynNode);
  return parentMaybe;
}

inline std::string to_constant(unsigned long a) {
  std::string constant = std::to_string(a) + "U";
  if (a > std::numeric_limits<unsigned int>().max()) constant += "L";
  return constant;
}

inline std::string to_constant(long a) {
  std::string constant = std::to_string(a);
  if (a > std::numeric_limits<int>().max()) constant += "L";
  return constant;
}

inline std::string to_constant(unsigned int a) {
  return to_constant((unsigned long) a);
}

inline std::string to_constant(unsigned short a) {
  return to_constant((unsigned long) a);
}

inline std::string to_constant(unsigned char a) {
  return to_constant((unsigned long) a);
}

inline std::string to_constant(int a) {
  return to_constant((long) a);
}

inline std::string to_constant(short a) {
  return to_constant((long) a);
}

inline std::string to_constant(char a) {
  return to_constant((long) a);
}

#endif // CLANG_UTILS_H
