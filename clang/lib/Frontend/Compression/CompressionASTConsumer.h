#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"

using namespace clang;

class CompressionASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<CompressionASTConsumer> {
  CompilerInstance &CI;
  Rewriter &R;

public:
  explicit CompressionASTConsumer(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}

  void HandleTranslationUnit(ASTContext &Context) override;
};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
