//
// Created by p on 19/12/2021.
//
#ifndef LLVM_CLANG_TRANSFORM_TRANSFORMAST_H
#define LLVM_CLANG_TRANSFORM_TRANSFORMAST_H

#include "clang/AST/ASTContext.h"
#include "clang/Sema/Sema.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang {

IdentifierInfo &createIdentifierInfo(ASTContext &C, std::string name);

/**
 * This creates a new record without any fields and attaches it as a top level
 * declaration.
 * @param C
 * @param S
 * @param name
 * @param kind
 * @param fields
 * @param completeDefinition if true, RecordDecl.completeDefinition() is called
 * which prevents fields from being added
 * @return
 */
CXXRecordDecl *createNewEmptyRecord(ASTContext &C, Sema &S, std::string name,
                                    TagTypeKind kind = TagTypeKind::Struct,
                                    bool completeDefinition = true);

CXXConstructorDecl *getDefaultNoArgConstructorDecl(CXXRecordDecl *recordDecl);

CXXRecordDecl *findRecordDeclByName(std::string name, TranslationUnitDecl *unitDecl);

Expr *getInitExpr(ASTContext &C, CXXRecordDecl *recordDecl);

class ASTTypeSwitcher : public RecursiveASTVisitor<ASTTypeSwitcher> {
  ASTContext &C;
  QualType oldType, newType;
  Expr *initExpr;

public:
  ASTTypeSwitcher(ASTContext &C, QualType oldType, QualType newType,
                  Expr *init = nullptr)
      : C(C), oldType(oldType), newType(newType), initExpr(init){};
  bool VisitVarDecl(VarDecl *D);
  bool VisitFunctionDecl(FunctionDecl *D);
  bool VisitFieldDecl(FieldDecl *D);
  bool VisitDeclRefExpr(DeclRefExpr *S);
  void ExecuteTreeTransform(Sema &S);
};
} // namespace clang

#endif // LLVM_CLANG_TRANSFORM_TRANSFORMAST_H
