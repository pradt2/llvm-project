#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

using namespace clang;

class CompressionASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<CompressionASTConsumer> {
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit CompressionASTConsumer(ASTContext &Ctx,
                                  SourceManager &SrcMgr,
                                  LangOptions &LangOpts,
                                  Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override;
};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
