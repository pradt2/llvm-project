#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

class CompressionASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<CompressionASTConsumer> {
  CompilerInstance &CI;
  Rewriter &R;

public:
  explicit CompressionASTConsumer(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}
};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
