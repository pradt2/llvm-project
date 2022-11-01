#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/PreprocessorOptions.h"
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

class ForceFloatLiteralASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<ForceFloatLiteralASTConsumer> {
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit ForceFloatLiteralASTConsumer(ASTContext &Ctx,
                                  SourceManager &SrcMgr,
                                  LangOptions &LangOpts,
                                  Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override;

  bool VisitFloatingLiteral(FloatingLiteral *FL);
};

class ForceFloatASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<ForceFloatASTConsumer> {
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  PreprocessorOptions &PP;
  std::map<std::string, std::string> &fileMap;

public:
  explicit ForceFloatASTConsumer(ASTContext &Ctx,
                                  SourceManager &SrcMgr,
                                  LangOptions &LangOpts,
                                 PreprocessorOptions &PP,
                                 std::map<std::string, std::string> &fileMap) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), PP(PP), fileMap(fileMap) {}

  void HandleTranslationUnit(ASTContext &Context) override;

  bool VisitDecl(Decl *D);

  void forceFloat();
};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
