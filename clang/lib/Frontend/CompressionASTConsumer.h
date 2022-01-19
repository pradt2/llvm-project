#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

#include "./CompressionCodeGen.h"

class SubExprFinder : public ASTConsumer,
                      public RecursiveASTVisitor<SubExprFinder> {
  Expr *child;
  bool found;

  bool _containsSubExpr(Expr *parent, Expr *child) {
    this->found = false;
    this->child = child;
    this->TraverseStmt(parent);
    return this->found;
  }

public:
  bool VisitExpr(Expr *E) {
    if (E == child)
      this->found = true;
    return true;
  }

  static bool containsSubExpr(Expr *parent, Expr *child) {
    bool contains = SubExprFinder()._containsSubExpr(parent, child);
    return contains;
  }
};

template<class ExprClass>
class SubExprOfTypeFinder : public ASTConsumer,
                            public RecursiveASTVisitor<SubExprFinder> {
  ExprClass *child;

  ExprClass* _containsSubExprOfType(Expr *parent) {
    this->child = nullptr;
    this->TraverseStmt(parent);
    return this->child;
  }

public:
  bool VisitExpr(Expr *E) {
    if (llvm::isa<ExprClass>(E) && child == nullptr)
      this->child = llvm::cast_or_null<ExprClass>(E);
    return true;
  }

  static ExprClass *containsSubExprOfType(Expr *parent) {
    ExprClass *child = SubExprOfTypeFinder<ExprClass>()._containsSubExprOfType(parent);
    return child;
  }
};

static QualType getTypeFromIndirectType(QualType type, std::string &ptrs) {
  while (type->isReferenceType() || type->isAnyPointerType()) {
    if (type->isReferenceType()) {
      type = type.getNonReferenceType();
      ptrs += "&";
    } else if (type->isAnyPointerType()) {
      type = type->getPointeeType();
      ptrs += "*";
    }
  }
  return type;
}

class RewriterASTConsumer : public ASTConsumer,
                            public RecursiveASTVisitor<RewriterASTConsumer> {
  typedef RecursiveASTVisitor<RewriterASTConsumer> base;

  CompilerInstance &CI;
  Rewriter &R;


private:

  class NewStructAdder : public ASTConsumer, public RecursiveASTVisitor<NewStructAdder> {
  private:
    Rewriter &R;
    CompilerInstance &CI;
  public:
    explicit NewStructAdder(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
      if (!isCompressionCandidate(decl)) return true;
      auto compressionCodeGen = CompressionCodeGen(decl, CI);
      std::string compressedStructDef = compressionCodeGen.getCompressedStructDef();
      R.InsertTextAfterToken(decl->getEndLoc(), ";\n" + compressedStructDef);
      return true;
    }
  };

  class VarDeclUpdater : public ASTConsumer, public RecursiveASTVisitor<VarDeclUpdater> {
  private:
    Rewriter &R;
    CompilerInstance &CI;
  public:
    explicit VarDeclUpdater(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitFieldDecl(FieldDecl *decl) {
      std::string ptrs = "";
      auto type = getTypeFromIndirectType(decl->getType(), ptrs);
      if (!type->isRecordType()) return true;
      auto *record = type->getAsRecordDecl();
      if (!isCompressionCandidate(record)) return true;
      auto compressionCodeGen = CompressionCodeGen(record, CI);
      R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
      if (!decl->hasInClassInitializer()) return true;
      Expr *initExpr = decl->getInClassInitializer();
      if (decl->getInClassInitStyle() == InClassInitStyle::ICIS_ListInit) { // TODO move to its own ASTConsumer?
        InitListExpr *initListExpr = llvm::cast<InitListExpr>(initExpr);
        std::string source = "{";
        for (unsigned int i = 0; i < initListExpr->getNumInits(); i++) {
          auto *initExpr = initListExpr->getInit(i);
          if (initExpr->getSourceRange().isInvalid()) continue;
          source += R.getRewrittenText(initExpr->getSourceRange()) + ", ";
        }
        source.pop_back();
        source.pop_back();
        source += "}";
        R.ReplaceText(initListExpr->getSourceRange(), source);
        R.InsertTextBefore(initListExpr->getBeginLoc(), "(");
        R.InsertTextAfterToken(initListExpr->getEndLoc(), ")");
      }
      else if (decl->getInClassInitStyle() == InClassInitStyle::ICIS_CopyInit) {
        // nothing to do, handled by constructor invocation rewrite
        return true;
      }
      return true;
    }

    bool VisitVarDecl(VarDecl *decl) {
      std::string ptrs = "";
      auto type = getTypeFromIndirectType(decl->getType(), ptrs);
      if (!type->isRecordType()) return true;
      auto *record = type->getAsRecordDecl();
      if (!isCompressionCandidate(record)) return true;
      auto compressionCodeGen = CompressionCodeGen(record, CI);
      R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
      if (!decl->hasInit()) return true;
      Expr *initExpr = decl->getInit();
      if (decl->getInitStyle() == VarDecl::InitializationStyle::ListInit) { // TODO move to its own ASTConsumer?
        InitListExpr *initListExpr = llvm::cast<InitListExpr>(initExpr);
        std::string source = "{";
        for (unsigned int i = 0; i < initListExpr->getNumInits(); i++) {
          auto *initExpr = initListExpr->getInit(i);
          if (initExpr->getSourceRange().isInvalid()) continue;
          source += R.getRewrittenText(initExpr->getSourceRange()) + ", ";
        }
        source.pop_back();
        source.pop_back();
        source += "}";
        R.ReplaceText(initListExpr->getSourceRange(), source);
        R.InsertTextBefore(initListExpr->getBeginLoc(), "(");
        R.InsertTextAfterToken(initListExpr->getEndLoc(), ")");
      }
      else if (decl->getInitStyle() == VarDecl::CInit) {
        // nothing to do, handled by constructor invocation rewrite
        return true;
      }
      return true;
    }
  };

  class ConstructorExprRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstructorExprRewriter> {
  private:
    Rewriter &R;
    CompilerInstance &CI;
  public:
    explicit ConstructorExprRewriter(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitCXXConstructExpr(CXXConstructExpr *decl) {
      if (decl->isElidable()) return true;
      std::string ptrs;
      auto constructType = getTypeFromIndirectType(decl->getType(), ptrs);
      if (!constructType->isRecordType()) return true;
      auto *constructDecl = constructType->getAsRecordDecl();
      if (!isCompressionCandidate(constructDecl)) return true;
      auto compressionCodeGen = CompressionCodeGen(constructDecl, CI);
      R.InsertTextBefore(decl->getBeginLoc(), compressionCodeGen.getCompressedStructName() + "(");
      R.InsertTextAfterToken(decl->getEndLoc(), ")");
      return true;
    }
  };

  class FunctionReturnTypeUpdater : public ASTConsumer, public RecursiveASTVisitor<FunctionReturnTypeUpdater> {
  private:
    Rewriter &R;
    CompilerInstance &CI;
  public:
    explicit FunctionReturnTypeUpdater(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitFunctionDecl(FunctionDecl *decl) {
      std::string ptrs = "";
      auto returnIndirectType = decl->getReturnType();
      auto returnType = getTypeFromIndirectType(returnIndirectType, ptrs);
      if (!returnType->isRecordType()) return true;
      auto *record = returnType->getAsRecordDecl();
      if (!isCompressionCandidate(record)) return true;
      auto compressionCodeGen = CompressionCodeGen(record, CI);
      R.ReplaceText(decl->getReturnTypeSourceRange(), compressionCodeGen.getCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
      return true;
    }
  };

  class ReadAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ReadAccessRewriter> {
  private:
    Rewriter &R;
    CompilerInstance &CI;

  public:
    explicit ReadAccessRewriter(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitMemberExpr(MemberExpr *expr) {
      auto *memberDecl = expr->getMemberDecl();
      if (!llvm::isa<FieldDecl>(memberDecl)) return true;
      auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
      if (fieldDecl == nullptr) return true;
      if (!isCompressionCandidate(fieldDecl)) return true;

      auto parents =
          CI.getASTContext().getParentMapContext().getParents(*expr);
      if (parents.size() != 1) {
        llvm::outs() << "Multiple parents of MemberExpr\n";
        return false;
      }

      std::string varName = R.getRewrittenText(SourceRange(expr->getBeginLoc(), expr->getMemberLoc().getLocWithOffset(-1)));

      auto parent = parents[0];
      auto parentNodeKind = parent.getNodeKind();
      if (parentNodeKind.KindId ==
          ASTNodeKind::NodeKindId::NKI_ImplicitCastExpr) {
        auto *implicitCast = parent.get<ImplicitCastExpr>();
        if (implicitCast->getCastKind() != CastKind::CK_LValueToRValue)
          return true;
        // value is read here, we know by the implicit lvalue to rvalue cast

        std::string source = CompressionCodeGen(fieldDecl->getParent(), CI).getGetterExpr(fieldDecl, varName);
        R.ReplaceText(SourceRange(expr->getBeginLoc(), expr->getEndLoc()),
                      source);
      }
      return true;
    }
  };

  class WriteAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<WriteAccessRewriter> {
  private:
    Rewriter &R;
    CompilerInstance &CI;

  public:
    explicit WriteAccessRewriter(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitMemberExpr(MemberExpr *expr) {
      auto *memberDecl = expr->getMemberDecl();
      if (!llvm::isa<FieldDecl>(memberDecl)) return true;
      auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
      if (fieldDecl == nullptr) return true;
      if (!isCompressionCandidate(fieldDecl)) return true;

      auto parents =
          CI.getASTContext().getParentMapContext().getParents(*expr);
      if (parents.size() != 1) {
        llvm::outs() << "Multiple parents of MemberExpr\n";
        return false;
      }

      auto parent = parents[0];
      auto parentNodeKind = parent.getNodeKind();
      if (parentNodeKind.KindId == ASTNodeKind::NKI_BinaryOperator) {
        // simple value assignment, e.g p.x = 1;
        auto *binaryOp = parent.get<BinaryOperator>();
        if (binaryOp->getOpcode() != clang::BO_Assign) return true;
        if (binaryOp->getLHS() != expr || SubExprFinder::containsSubExpr(binaryOp->getRHS(), expr)) {
          llvm::outs() << "Expr is both read and written to?\n";
          return true;
        }

        std::string varName = R.getRewrittenText(SourceRange(expr->getBeginLoc(), expr->getMemberLoc().getLocWithOffset(-1)));

        auto rhsCurrentExpr = R.getRewrittenText(binaryOp->getRHS()->getSourceRange());
        std::string source = CompressionCodeGen(fieldDecl->getParent(), CI).getSetterExpr(fieldDecl, varName, rhsCurrentExpr);
        R.ReplaceText(binaryOp->getSourceRange(), source);
      } else if (parentNodeKind.KindId == ASTNodeKind::NKI_CompoundAssignOperator) {
        llvm::outs() << "To be implemented\n";
      }
      return true;
    }
  };

public:
  RewriterASTConsumer(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    NewStructAdder(R, CI).HandleTranslationUnit(Context);
    VarDeclUpdater(R, CI).HandleTranslationUnit(Context);
    ConstructorExprRewriter(R, CI).HandleTranslationUnit(Context);
    FunctionReturnTypeUpdater(R, CI).HandleTranslationUnit(Context);
    ReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
    WriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
  }

};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
