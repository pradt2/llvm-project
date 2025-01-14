#pragma once

#include <optional>

using namespace clang;

std::optional<ViewAttr*> DeclaresView(TypeAliasDecl *D) {
  if (!D->hasAttr<ViewAttr>()) return std::nullopt;
  auto *viewAttr = D->getAttr<ViewAttr>();
  return viewAttr;
}

SemaRecordDecl CreateView(CompoundStmt *Stmt, CXXRecordDecl *D) {

}
