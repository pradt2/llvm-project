//
// Created by p on 22/03/2022.
//

#ifndef CLANG_MPIDATATYPESMAPPERASTCONSUMER_H
#define CLANG_MPIDATATYPESMAPPERASTCONSUMER_H

#include "MpiMappingGenerator.h"

class MpiDatatypesMapperASTConsumer : public ASTConsumer,
                                      public RecursiveASTVisitor<MpiDatatypesMapperASTConsumer> {
  CompilerInstance &CI;
  Rewriter &R;

  class MpiSupportAdder : public ASTConsumer , public RecursiveASTVisitor<MpiSupportAdder> {
  private:
    Rewriter &R;
    CompilerInstance &CI;

  public:
    explicit MpiSupportAdder(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitCXXMethodDecl(CXXMethodDecl *D) {
      MpiMappingGenerator adder;
      if (!adder.isMpiMappingCandidate(D)) return true;

      auto code = adder.getMpiMappingMethodBody(D);

      if (D->hasBody()) {
        auto bodyRange = D->getBody()->getSourceRange();
        R.ReplaceText(bodyRange, " {\n" + code + "\n}");
      } else {
        SourceLocation methodSigEndLoc = D->getEndLoc();
        R.InsertTextAfterToken(methodSigEndLoc, " {\n" + code + "\n}");
      }

      return true;
    }
  };

public:
  MpiDatatypesMapperASTConsumer(CompilerInstance &CI) : CI(CI), R(CI.getSourceManager().getRewriter()) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    MpiSupportAdder(R, CI).HandleTranslationUnit(Context);
  }

};

#endif // CLANG_MPIDATATYPESMAPPERASTCONSUMER_H
