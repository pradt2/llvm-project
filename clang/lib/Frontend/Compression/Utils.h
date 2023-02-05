//
// Created by p on 04/09/22.
//

#ifndef CLANG_UTILS_H
#define CLANG_UTILS_H

#include <string>
#include <limits>

template<typename T>
inline const T* getParentNodeOfType(ASTContext &Ctx, const Expr *E, ASTNodeKind::NodeKindId nodeKind) {
  auto parents = Ctx.getParents(*E);
  auto parent = parents[0];
  while (!parents.empty()) {
    parent = parents[0];
    if (parent.getNodeKind().KindId == nodeKind) break;
    parents = Ctx.getParents(parent);
  }

  if (parents.empty()) return NULL;
  return parent.get<T>();
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
