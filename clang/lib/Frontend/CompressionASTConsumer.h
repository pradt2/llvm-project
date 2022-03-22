#ifndef CLANG_COMPRESSIONASTCONSUMER_H
#define CLANG_COMPRESSIONASTCONSUMER_H

#include "./Compression/CompressionCodeGenResolver.h"
#include "./MPI/MpiMappingGenerator.h"

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
    if (E == child) {
      this->found = true;
      return false;
    }
    return true;
  }

  static bool containsSubExpr(Expr *parent, Expr *child) {
    bool contains = SubExprFinder()._containsSubExpr(parent, child);
    return contains;
  }
};

template<class ExprClass>
class SubExprOfTypeFinder : public ASTConsumer,
                            public RecursiveASTVisitor<SubExprOfTypeFinder<ExprClass>> {
  ExprClass *child;
  Expr *_parent;

  ExprClass* _containsSubExprOfType(Expr *parent) {
    this->child = nullptr;
    this->_parent = parent;
    this->TraverseStmt(parent);
    return this->child;
  }

public:
  bool VisitExpr(Expr *E) {
    if (llvm::isa<ExprClass>(E)) {
      auto *childCandidate = llvm::cast<ExprClass>(E);
      if (childCandidate != _parent) {
        child = childCandidate;
        return false;
      }
    }
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

class CompressionASTConsumer : public ASTConsumer,
                               public RecursiveASTVisitor<CompressionASTConsumer> {
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
      auto compressionCodeGen = CompressionCodeGenResolver(decl, CI);
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
      auto compressionCodeGen = CompressionCodeGenResolver(record, CI);
      R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
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

    bool VisitCallExpr(CallExpr *expr) {
      FunctionDecl *functionDecl = expr->getDirectCallee();
      if (functionDecl == nullptr) return true;
      if (!functionDecl->isStatic()) return true;
      DeclContext *parentDecl = functionDecl->getParent();
      if (!parentDecl->isRecord()) return true;
      RecordDecl *recordDecl = llvm::cast<RecordDecl>(parentDecl);
      if (!isCompressionCandidate(recordDecl)) return true;
      SourceRange rangeToReplace = expr->getCallee()->getSourceRange();
      CompressionCodeGenResolver compressionCodeGenResolver = CompressionCodeGenResolver(recordDecl, CI);
      std::string newSource = compressionCodeGenResolver.getFullyQualifiedCompressedStructName() + "::" + functionDecl->getNameAsString();
      R.ReplaceText(rangeToReplace, newSource);
      return true;
    }

    bool VisitVarDecl(VarDecl *decl) {
      if (decl->getParentFunctionOrMethod() != nullptr && decl->getParentFunctionOrMethod()->getParent()->isRecord()) {
        RecordDecl *recordDecl = llvm::cast<RecordDecl>(decl->getParentFunctionOrMethod()->getParent());
        if (isCompressionCandidate(recordDecl)) return true; // do not change args of methods declared on the compressed type itself
      }
      std::string ptrs = "";
      auto type = getTypeFromIndirectType(decl->getType(), ptrs);
      if (!type->isRecordType()) return true;
      auto *record = type->getAsRecordDecl();
      if (!isCompressionCandidate(record)) return true;
      auto compressionCodeGen = CompressionCodeGenResolver(record, CI);
      R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
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
      auto compressionCodeGen = CompressionCodeGenResolver(constructDecl, CI);
      R.InsertTextBefore(decl->getBeginLoc(), compressionCodeGen.getFullyQualifiedCompressedStructName() + "(");
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
      auto compressionCodeGen = CompressionCodeGenResolver(record, CI);
      R.ReplaceText(decl->getReturnTypeSourceRange(), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
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
      if (!isNonIndexAccessCompressionCandidate(fieldDecl)) return true;

      auto parents =
          CI.getASTContext().getParents(*expr);
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

        std::string source =
            CompressionCodeGenResolver(fieldDecl->getParent(), CI).getGetterExpr(fieldDecl, varName);
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
      if (!isNonIndexAccessCompressionCandidate(fieldDecl)) return true;

      auto parents =
          CI.getASTContext().getParents(*expr);
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
        std::string source =
            CompressionCodeGenResolver(fieldDecl->getParent(), CI).getSetterExpr(fieldDecl, varName, rhsCurrentExpr);
        R.ReplaceText(binaryOp->getSourceRange(), source);
      } else if (parentNodeKind.KindId == ASTNodeKind::NKI_CompoundAssignOperator) {
        llvm::outs() << "To be implemented\n";
      }
      return true;
    }
  };

  class PragmaPackAdder : public ASTConsumer, public RecursiveASTVisitor<PragmaPackAdder> {
  private:

    class CompressibleTypeFieldsFinder : public ASTConsumer, public RecursiveASTVisitor<CompressibleTypeFieldsFinder> {
      bool found;
    public:
      bool VisitFieldDecl(FieldDecl *d) {
        if (!d->getType()->isRecordType()) return true;
        auto *rd = d->getType()->getAsRecordDecl();
        if (isCompressionCandidate(rd)) {
          found = true;
          return false;
        }
        return true;
      }

      bool doesContainCompressibleStructs(RecordDecl *rd) {
        this->found = false;
        this->TraverseDecl(rd);
        return this->found;
      }

    } compressibleTypeFieldsFinder;

    Rewriter &R;
    CompilerInstance &CI;
  public:
    explicit PragmaPackAdder(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitRecordDecl(RecordDecl *d) {
      if (!this->compressibleTypeFieldsFinder.doesContainCompressibleStructs(d)) return true;
      R.InsertTextBefore(d->getBeginLoc(), "#pragma pack(push, 1)\n");
      R.InsertTextAfterToken(d->getEndLoc(), ";\n#pragma pack(pop)\n");
      return true;
    }
  };

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

      auto code = adder.getMpiMappingMethodBody(D->getParent());

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

  class ConstSizeArrReadAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstSizeArrReadAccessRewriter> {
  private:
    Rewriter &R;
    CompilerInstance &CI;

    bool hasParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
      auto parentList = CI.getASTContext().getParents(child);
      while (parentList.size() == 1) {
        auto parent = parentList[0];
        if (parent.getNodeKind().KindId == kindId) {
          return true;
        }
        parentList = CI.getASTContext().getParents(parent);
      }
      return false;
    }

    template<typename T>
    const T* getParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
      auto parentList = CI.getASTContext().getParents(child);
      while (parentList.size() == 1) {
        auto parent = parentList[0];
        if (parent.getNodeKind().KindId == kindId) {
          return parent.get<T>();
        }
        parentList = CI.getASTContext().getParents(parent);
      }
      return NULL;
    }

  public:
    explicit ConstSizeArrReadAccessRewriter(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
      if (hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_ArraySubscriptExpr)) return true; // we're in the middle of a multi-dim arr ref
      if (hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_ArrayInitLoopExpr)) return true; // we're in the middle of an autogenerated constructor
      if (hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_BinaryOperator)) {
        auto *binaryExpr = getParentOfKind<BinaryOperator>(*e, ASTNodeKind::NodeKindId::NKI_BinaryOperator);
        if (SubExprFinder::containsSubExpr(binaryExpr->getLHS(), e)) return true; // expression in LHS of the binary operator expr
      }
      std::vector<std::string> idxs;
      SubExprOfTypeFinder<ArraySubscriptExpr> finder;
      auto *parentExpr = e;
      while (true) {
          std::string idx = R.getRewrittenText(parentExpr->getRHS()->getSourceRange());
          idxs.push_back(idx);
          auto *subExpr = finder.containsSubExprOfType(parentExpr);
          if (subExpr == NULL) break;
          parentExpr = subExpr;
      }
      SubExprOfTypeFinder<MemberExpr> memberExprFinder;
      auto *memberExpr = memberExprFinder.containsSubExprOfType(parentExpr);
      if (memberExpr == NULL) {
        return true;
      }

      auto *memberDecl = memberExpr->getMemberDecl();
      if (!llvm::isa<FieldDecl>(memberDecl)) return true;
      auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
      if (fieldDecl == nullptr) return true;
      if (!isIndexAccessCompressionCandidate(fieldDecl)) return true;

      std::string varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));

      std::reverse(idxs.begin(), idxs.end());

      std::string source =
          CompressionCodeGenResolver(fieldDecl->getParent(), CI).getGetterExpr(fieldDecl, varName, idxs);

      R.ReplaceText(e->getSourceRange(), source);
      return true;
    }
  };

  class ConstSizeArrWriteAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstSizeArrWriteAccessRewriter> {
  private:
    Rewriter &R;
    CompilerInstance &CI;

    bool hasParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
      auto parentList = CI.getASTContext().getParents(child);
      while (parentList.size() == 1) {
        auto parent = parentList[0];
        if (parent.getNodeKind().KindId == kindId) {
          return true;
        }
        parentList = CI.getASTContext().getParents(parent);
      }
      return false;
    }

    template<typename T>
    const T* getParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
      auto parentList = CI.getASTContext().getParents(child);
      while (parentList.size() == 1) {
        auto parent = parentList[0];
        if (parent.getNodeKind().KindId == kindId) {
          return parent.get<T>();
        }
        parentList = CI.getASTContext().getParents(parent);
      }
      return NULL;
    }

  public:
    explicit ConstSizeArrWriteAccessRewriter(Rewriter &R, CompilerInstance &CI) : R(R), CI(CI) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      TraverseDecl(D);
    }

    bool VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
      if (hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_ArraySubscriptExpr)) return true; // we're in the middle of a multi-dim arr ref
      if (hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_ArrayInitLoopExpr)) return true; // we're in the middle of an autogenerated constructor
      if (!hasParentOfKind(*e, ASTNodeKind::NodeKindId::NKI_BinaryOperator)) return true;
      std::vector<std::string> idxs;
      SubExprOfTypeFinder<ArraySubscriptExpr> finder;
      auto *parentExpr = e;
      while (true) {
        std::string idx = R.getRewrittenText(parentExpr->getRHS()->getSourceRange());
        idxs.push_back(idx);
        auto *subExpr = finder.containsSubExprOfType(parentExpr);
        if (subExpr == NULL) break;
        parentExpr = subExpr;
      }
      SubExprOfTypeFinder<MemberExpr> memberExprFinder;
      auto *memberExpr = memberExprFinder.containsSubExprOfType(parentExpr);
      if (memberExpr == NULL) {
        return true;
      }

      auto *memberDecl = memberExpr->getMemberDecl();
      if (!llvm::isa<FieldDecl>(memberDecl)) return true;
      auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
      if (fieldDecl == nullptr) return true;
      if (!isIndexAccessCompressionCandidate(fieldDecl)) return true;

      auto *binaryExpr = getParentOfKind<BinaryOperator>(*e, ASTNodeKind::NodeKindId::NKI_BinaryOperator);
      if (SubExprFinder::containsSubExpr(binaryExpr->getRHS(), e)) return true; // expression in RHS of the binary operator expr

      std::string varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));

      std::string val = R.getRewrittenText(binaryExpr->getRHS()->getSourceRange());

      std::reverse(idxs.begin(), idxs.end());

      std::string source =
          CompressionCodeGenResolver(fieldDecl->getParent(), CI).getSetterExpr(fieldDecl, varName, idxs, val);

      R.ReplaceText(binaryExpr->getSourceRange(), source);
      return true;
    }
  };


public:
  CompressionASTConsumer(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    NewStructAdder(R, CI).HandleTranslationUnit(Context);
    VarDeclUpdater(R, CI).HandleTranslationUnit(Context);
    ConstructorExprRewriter(R, CI).HandleTranslationUnit(Context);
    FunctionReturnTypeUpdater(R, CI).HandleTranslationUnit(Context);
    ReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
    ConstSizeArrReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
    WriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
    ConstSizeArrWriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
    PragmaPackAdder(R, CI).HandleTranslationUnit(Context);
    MpiSupportAdder(R, CI).HandleTranslationUnit(Context);
  }

};

#endif // CLANG_COMPRESSIONASTCONSUMER_H
