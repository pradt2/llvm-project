
#include "clang/Transform/TransformAST.h"

namespace clang {

bool isPointerRefArrType(QualType type) {
  return type->isPointerType() || type->isReferenceType() || type->isAggregateType();
}

QualType getImmediatePointeeType(QualType type) {
  if (type->isPointerType()) return type->getPointeeType();
  if (type->isReferenceType()) return type.getNonReferenceType();
  if (type->isArrayType()) {} // TODO
}

QualType createNewPointerRefArrType(ASTContext &C, QualType oldType, QualType newType) {
  enum IndirectKind {PTR, L_REF, R_REF, ARR};
  auto kindsVec = llvm::SmallVector<IndirectKind>();

  while (isPointerRefArrType(oldType)) {
    if (oldType->isPointerType()) {
      kindsVec.push_back(PTR);
      oldType = oldType->getPointeeType();
    } else if (oldType->isLValueReferenceType()) {
      kindsVec.push_back(L_REF);
      oldType = oldType.getNonReferenceType();
    } else if (oldType->isRValueReferenceType()) {
      kindsVec.push_back(R_REF);
      oldType = oldType.getNonReferenceType();
    } else if (oldType->isArrayType()) {
      kindsVec.push_back(ARR);
      oldType = QualType(oldType->getArrayElementTypeNoTypeQual(), 0);
    }
  }

  for (int i = kindsVec.size() - 1; i >= 0; i--) {
    IndirectKind kind = kindsVec[i];
    switch (kind) {
    case PTR:
      newType = C.getPointerType(newType);
      break;
    case L_REF:
      newType = C.getLValueReferenceType(newType);
      break;
    case R_REF:
      newType = C.getRValueReferenceType(newType);
      break;
    case ARR:
      break;
      // TODO
    }
  }

  return newType;
}

bool ASTTypeSwitcher::VisitVarDecl(VarDecl *D) {
  auto declType = D->getType();
  while (isPointerRefArrType(declType)) {
    declType = getImmediatePointeeType(declType);
  }
  if (declType != this->oldType) return true;
  auto newType = createNewPointerRefArrType(this->C, D->getType(), this->newType);
  auto *typeSourceInfo = this->C.getTrivialTypeSourceInfo(newType);
  D->setType(newType);
  D->setTypeSourceInfo(typeSourceInfo);
  if (this->initExpr && D->hasInit()) D->setInit(this->initExpr);
  return true;
}

bool ASTTypeSwitcher::VisitFunctionDecl(FunctionDecl *D) {
  return true;
}

bool ASTTypeSwitcher::VisitFieldDecl(FieldDecl *D) {
    return true;
}

bool ASTTypeSwitcher::VisitDeclRefExpr(DeclRefExpr *S) {
  auto declType = S->getType();
  while (isPointerRefArrType(declType)) {
    declType = getImmediatePointeeType(declType);
  }
  if (declType != this->oldType) return true;
  auto newType = createNewPointerRefArrType(this->C, S->getType(), this->newType);
  S->setType(newType);
  return true;
}

CXXRecordDecl *createNewEmptyRecord(ASTContext &C, Sema &S, std::string name, TagTypeKind kind, bool completeDefinition) {
  auto &classIdentifierInfo = createIdentifierInfo(C, name);
  auto *translationUnitDecl = C.getTranslationUnitDecl();
  auto *newRecord = CXXRecordDecl::Create(C, kind, translationUnitDecl, SourceLocation(), SourceLocation(), &classIdentifierInfo);
  newRecord->startDefinition();
  newRecord->setReferenced();
  S.DeclareImplicitDefaultConstructor(newRecord); // this already calls CXXRecordDecl->addDecl
  if (completeDefinition) newRecord->completeDefinition();
  C.getTranslationUnitDecl()->addDecl(newRecord);
  return newRecord;
}

IdentifierInfo &createIdentifierInfo(ASTContext &C, std::string name) {
  return C.Idents.getOwn(llvm::StringRef(name));
}

CXXConstructorDecl *getDefaultNoArgConstructorDecl(CXXRecordDecl *recordDecl) {
  if (!recordDecl->isCompleteDefinition()) {
    recordDecl = recordDecl->getDefinition();
  }
  if (!recordDecl) return nullptr;
  for (auto *method : recordDecl->methods()) {
    if (!llvm::isa<CXXConstructorDecl>(method)) continue;
    auto *constructorDecl = llvm::cast<CXXConstructorDecl>(method);
    if (constructorDecl->getNumParams() != 0) continue;
    return constructorDecl;
  }
  return nullptr;
}

Expr *getInitExpr(ASTContext &C, CXXRecordDecl *recordDecl) {
  auto *constructorDecl = getDefaultNoArgConstructorDecl(recordDecl);
  if (!constructorDecl) return nullptr;
  auto recordType = C.getRecordType(recordDecl);
  auto *expr = CXXConstructExpr::Create(C, recordType, SourceLocation(), constructorDecl,false, llvm::ArrayRef<Expr *>(nullptr, (size_t) 0), false, false, false, false, CXXConstructExpr::ConstructionKind::CK_Complete, SourceRange());
  return expr;
}

}
