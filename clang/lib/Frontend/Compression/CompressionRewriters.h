//
// Created by p on 16/08/22.
//

#ifndef CLANG_COMPRESSIONREWRITERS_H
#define CLANG_COMPRESSIONREWRITERS_H

#include "CompressionCodeGenResolver.h"

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

class StaticMethodCallUpdater : public ASTConsumer, public RecursiveASTVisitor<StaticMethodCallUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit StaticMethodCallUpdater(ASTContext &Ctx,
                                   SourceManager &SrcMgr,
                                   LangOptions &LangOpts,
                                   Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitCallExpr(CallExpr *expr) {                      // this replaces static method calls,
    FunctionDecl *functionDecl = expr->getDirectCallee();   // probably only useful for the MPI auto mapping atm
    if (functionDecl == nullptr) return true;
    if (!functionDecl->isStatic()) return true;
    DeclContext *parentDecl = functionDecl->getParent();
    if (!parentDecl->isRecord()) return true;
    RecordDecl *recordDecl = llvm::cast<RecordDecl>(parentDecl);
    if (!isCompressionCandidate(recordDecl)) return true;
    SourceRange rangeToReplace = expr->getCallee()->getSourceRange();
    CompressionCodeGenResolver compressionCodeGenResolver = CompressionCodeGenResolver(recordDecl, Ctx, SrcMgr, LangOpts, R);
    std::string newSource = compressionCodeGenResolver.getFullyQualifiedCompressedStructName() + "::" + functionDecl->getNameAsString();
    R.ReplaceText(rangeToReplace, newSource);
    return true;
  }

};

class ConstructorAndInitExprRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstructorAndInitExprRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit ConstructorAndInitExprRewriter(ASTContext &Ctx,
                                          SourceManager &SrcMgr,
                                          LangOptions &LangOpts,
                                          Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *decl) {
    if (decl->isElidable()) return true;
    if (decl->getNumArgs() == 0) return true;
    if (decl->getConstructor()->isImplicit()) return true;
    std::string ptrs;
    auto constructType = getTypeFromIndirectType(decl->getType(), ptrs);
    if (!constructType->isRecordType()) return true;
    auto *constructDecl = constructType->getAsRecordDecl();
    if (!isCompressionCandidate(constructDecl)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(constructDecl, Ctx, SrcMgr, LangOpts, R);
    R.InsertTextBefore(decl->getBeginLoc(), compressionCodeGen.getFullyQualifiedCompressedStructName() + "(");
    R.InsertTextAfterToken(decl->getEndLoc(), ")");
    return true;
  }

  bool VisitInitListExpr(InitListExpr *initListExpr) {
    RecordDecl *recordDecl = initListExpr->getType()->getAsRecordDecl();
    if (!recordDecl) return true;
    if (!isCompressionCandidate(recordDecl)) return true;

    std::string source = "{";
    for (unsigned int i = 0; i < initListExpr->getNumInits(); i++) {
      auto *initExpr = initListExpr->getInit(i);
      if (initExpr->getSourceRange().isInvalid()) continue;
      source += R.getRewrittenText(initExpr->getSourceRange()) + ", ";
    }
    source.pop_back();
    source.pop_back();
    source += "}";
    R.ReplaceText(initListExpr->getSourceRange(),  "(" + source + ")");
    return true;
  }
};

class ReadAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ReadAccessRewriter> {

private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit ReadAccessRewriter(ASTContext &Ctx,
                              SourceManager &SrcMgr,
                              LangOptions &LangOpts,
                              Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  bool VisitMemberExpr(MemberExpr *expr) {
    auto *memberDecl = expr->getMemberDecl();
    if (!llvm::isa<FieldDecl>(memberDecl)) return true;
    auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
    if (fieldDecl == nullptr) return true;
    if (!isNonIndexAccessCompressionCandidate(fieldDecl)) return true;

    auto parents =
        Ctx.getParents(*expr);
    if (parents.size() != 1) {
      llvm::outs() << "Multiple parents of MemberExpr\n";
      return false;
    }

    std::string varName;

    // Member expression 'var.field':
    //  - var is the base
    //  - field is the member
    Expr *baseExpr = expr->getBase();
    if (llvm::isa<CXXThisExpr>(baseExpr)) {
      CXXThisExpr *thisExpr = llvm::cast<CXXThisExpr>(baseExpr);
      varName = "this->";
    } else {
      varName = R.getRewrittenText(SourceRange(expr->getBeginLoc(), expr->getMemberLoc().getLocWithOffset(-1)));
    }

    auto parent = parents[0];
    auto parentNodeKind = parent.getNodeKind();
    if (parentNodeKind.KindId ==
        ASTNodeKind::NodeKindId::NKI_ImplicitCastExpr) {
      auto *implicitCast = parent.get<ImplicitCastExpr>();
      if (implicitCast->getCastKind() != CastKind::CK_LValueToRValue)
        return true;
      // value is read here, we know by the implicit lvalue to rvalue cast

      std::string source =
          CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getGetterExpr(fieldDecl, varName);
      R.ReplaceText(SourceRange(expr->getBeginLoc(), expr->getEndLoc()),
                    source);
    }
    return true;
  }

};

class WriteAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<WriteAccessRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit WriteAccessRewriter(ASTContext &Ctx,
                               SourceManager &SrcMgr,
                               LangOptions &LangOpts,
                               Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  bool VisitMemberExpr(MemberExpr *expr) {
    auto *memberDecl = expr->getMemberDecl();
    if (!llvm::isa<FieldDecl>(memberDecl)) return true;
    auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);
    if (fieldDecl == nullptr) return true;
    if (!isNonIndexAccessCompressionCandidate(fieldDecl)) return true;

    auto parents =
        Ctx.getParents(*expr);
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

      std::string varName;
      // Member expression 'var.field':
      //  - var is the base
      //  - field is the member
      Expr *baseExpr = expr->getBase();
      if (llvm::isa<CXXThisExpr>(baseExpr)) {
        varName = "this->";
      } else {
        varName = R.getRewrittenText(SourceRange(expr->getBeginLoc(), expr->getMemberLoc().getLocWithOffset(-1)));
      }

      auto rhsCurrentExpr = R.getRewrittenText(binaryOp->getRHS()->getSourceRange());
      std::string source =
          CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getSetterExpr(fieldDecl, varName, rhsCurrentExpr);
      R.ReplaceText(binaryOp->getSourceRange(), source);
    } else if (parentNodeKind.KindId == ASTNodeKind::NKI_CompoundAssignOperator) {
      llvm::outs() << "To be implemented\n";
    }
    return true;
  }

};

class ConstSizeArrReadAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstSizeArrReadAccessRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

  bool hasParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
    auto parentList = Ctx.getParents(child);
    while (parentList.size() == 1) {
      auto parent = parentList[0];
      if (parent.getNodeKind().KindId == kindId) {
        return true;
      }
      parentList = Ctx.getParents(parent);
    }
    return false;
  }

  template<typename T>
  const T* getParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
    auto parentList = Ctx.getParents(child);
    while (parentList.size() == 1) {
      auto parent = parentList[0];
      if (parent.getNodeKind().KindId == kindId) {
        return parent.get<T>();
      }
      parentList = Ctx.getParents(parent);
    }
    return NULL;
  }

public:
  explicit ConstSizeArrReadAccessRewriter(ASTContext &Ctx,
                                          SourceManager &SrcMgr,
                                          LangOptions &LangOpts,
                                          Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

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

    // Member expression 'var.field':
    //  - var is the base
    //  - field is the member
    std::string varName;
    Expr *baseExpr = memberExpr->getBase();
    if (llvm::isa<CXXThisExpr>(baseExpr)) {
      CXXThisExpr *thisExpr = llvm::cast<CXXThisExpr>(baseExpr);
      varName = "this->";
    } else {
      varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));
    }

    std::reverse(idxs.begin(), idxs.end());

    std::string source =
        CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getGetterExpr(fieldDecl, varName, idxs);

    R.ReplaceText(e->getSourceRange(), source);
    return true;
  }
};

class ConstSizeArrWriteAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstSizeArrWriteAccessRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

  bool hasParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
    auto parentList = Ctx.getParents(child);
    while (parentList.size() == 1) {
      auto parent = parentList[0];
      if (parent.getNodeKind().KindId == kindId) {
        return true;
      }
      parentList = Ctx.getParents(parent);
    }
    return false;
  }

  template<typename T>
  const T* getParentOfKind(Expr &child, ASTNodeKind::NodeKindId kindId) {
    auto parentList = Ctx.getParents(child);
    while (parentList.size() == 1) {
      auto parent = parentList[0];
      if (parent.getNodeKind().KindId == kindId) {
        return parent.get<T>();
      }
      parentList = Ctx.getParents(parent);
    }
    return NULL;
  }

public:
  explicit ConstSizeArrWriteAccessRewriter(ASTContext &Ctx,
                                           SourceManager &SrcMgr,
                                           LangOptions &LangOpts,
                                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

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

    // Member expression 'var.field':
    //  - var is the base
    //  - field is the member
    std::string varName;
    Expr *baseExpr = memberExpr->getBase();
    if (llvm::isa<CXXThisExpr>(baseExpr)) {
      CXXThisExpr *thisExpr = llvm::cast<CXXThisExpr>(baseExpr);
      varName = "this->";
    } else {
      varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));
    }

    std::string val = R.getRewrittenText(binaryExpr->getRHS()->getSourceRange());

    std::reverse(idxs.begin(), idxs.end());

    std::string source =
        CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getSetterExpr(fieldDecl, varName, idxs, val);

    R.ReplaceText(binaryExpr->getSourceRange(), source);
    return true;
  }
};

class ExprUpdater {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  ExprUpdater(ASTContext &Ctx,
              SourceManager &SrcMgr,
              LangOptions &LangOpts,
              Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleStmt(Stmt *stmt) {
    if (!stmt) return;

    StaticMethodCallUpdater(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
    ConstructorAndInitExprRewriter(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
    ReadAccessRewriter(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
    ConstSizeArrReadAccessRewriter(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
    WriteAccessRewriter(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
    ConstSizeArrWriteAccessRewriter(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
  }

};

class FunctionUpdater : public ASTConsumer, public RecursiveASTVisitor<FunctionUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit FunctionUpdater(ASTContext &Ctx,
                           SourceManager &SrcMgr,
                           LangOptions &LangOpts,
                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

public:

  bool VisitFunctionDecl(FunctionDecl *decl) {
    ExprUpdater(Ctx, SrcMgr, LangOpts, R).HandleStmt(decl->getBody()); // converts expressions inside the function

    if (llvm::isa<CXXMethodDecl>(decl)) { // changes return type
      CXXMethodDecl *methodDecl = llvm::cast<CXXMethodDecl>(decl);
      if (isCompressionCandidate(methodDecl->getParent())) return true;
    }

    std::string ptrs = "";
    auto returnIndirectType = decl->getReturnType();
    auto returnType = getTypeFromIndirectType(returnIndirectType, ptrs);
    if (!returnType->isRecordType()) return true;
    auto *record = returnType->getAsRecordDecl();
    if (!isCompressionCandidate(record)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
    R.ReplaceText(decl->getReturnTypeSourceRange(), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
    return true;
  }

  bool VisitVarDecl(VarDecl *decl) { // changes local var types, incl. function args
    std::string ptrs = "";
    auto type = getTypeFromIndirectType(decl->getType(), ptrs);
    if (!type->isRecordType()) return true;
    auto *record = type->getAsRecordDecl();
    if (!isCompressionCandidate(record)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
    R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
    return true;
  }
};


class NewStructForwardDeclAdder : public ASTConsumer, public RecursiveASTVisitor<NewStructForwardDeclAdder> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit NewStructForwardDeclAdder(ASTContext &Ctx,
                                     SourceManager &SrcMgr,
                                     LangOptions &LangOpts,
                                     Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
    if (!isCompressionCandidate(decl)) return true;
    while ((decl = decl->getPreviousDecl())) {
      auto compressionCodeGen = CompressionCodeGenResolver(decl, Ctx, SrcMgr, LangOpts, R);
      std::string compressedStructName = compressionCodeGen.getCompressedStructName();
      R.InsertTextAfterToken(decl->getEndLoc(), ";\n struct " + compressedStructName + ";\n");
    }
    return true;
  }
};

class FriendStructAdder : public ASTConsumer, public RecursiveASTVisitor<FriendStructAdder> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit FriendStructAdder(ASTContext &Ctx,
                             SourceManager &SrcMgr,
                             LangOptions &LangOpts,
                             Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
    if (!isCompressionCandidate(decl)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(decl, Ctx, SrcMgr, LangOpts, R);
    std::string compressedStructName = compressionCodeGen.getCompressedStructName();
    auto &srcMgr = R.getSourceMgr();
    auto loc = decl->getBraceRange().getBegin();
    R.InsertTextAfterToken(loc, "\n friend struct " + compressedStructName + ";\n");
    return true;
  }
};

class FieldDeclUpdater : public ASTConsumer, public RecursiveASTVisitor<FieldDeclUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit FieldDeclUpdater(ASTContext &Ctx,
                            SourceManager &SrcMgr,
                            LangOptions &LangOpts,
                            Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

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
    auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
    R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
    ExprUpdater(Ctx, SrcMgr, LangOpts, R).HandleStmt(decl->getInClassInitializer()); // this converts initializers
    return true;
  }

};

class PragmaPackAdder : public ASTConsumer, public RecursiveASTVisitor<PragmaPackAdder> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

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

public:
  explicit PragmaPackAdder(ASTContext &Ctx,
                           SourceManager &SrcMgr,
                           LangOptions &LangOpts,
                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

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

class NewStructAdder : public ASTConsumer, public RecursiveASTVisitor<NewStructAdder> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit NewStructAdder(ASTContext &Ctx,
                          SourceManager &SrcMgr,
                          LangOptions &LangOpts,
                          Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  std::string getMethodStr(FunctionDecl *decl, Rewriter &R, std::string noexceptStr = "") {
    // we have to get the signature and body separately, because they can be in separate files,
    // and in such cases decl->getBodyRBrace points to the cpp file instead of the h file,
    // and we cannot get a working SourceRange

    std::string method;

    SourceRange declRange = decl->getSourceRange();
    SourceRange bodyRange = decl->getBody()->getSourceRange();


    if (declRange.fullyContains(bodyRange)) {
      method = R.getRewrittenText(decl->getSourceRange()); // for when the body is provided in the h file in the declaration
    } else {
      // for when the decl and body are split into different (h and cpp) files
      method = R.getRewrittenText(decl->getSourceRange()) // method signature
               + " "
               + noexceptStr // I don't know why this is sometimes necessary
                             // but sometimes in Peano's compressed constructors I get
                             // peano4/grid/AutomatonState.h:452:39: error: 'AutomatonState__PACKED' is missing exception specification 'noexcept'
               + " "
               + R.getRewrittenText(decl->getBody()->getSourceRange()); // body
    }
    return method;
  }

  std::string getMethod(CXXMethodDecl *decl, std::string recordFullyQualifiedName) {
    std::string method;
    Rewriter r(SrcMgr, LangOpts);

    FunctionUpdater(Ctx, SrcMgr, LangOpts, r).TraverseDecl(decl);

    std::string functionName = recordFullyQualifiedName + "::" + decl->getDeclName().getAsString();

    r.ReplaceText(decl->getNameInfo().getSourceRange(), functionName); // replace function name with fully qualified struct name + name

    method = getMethodStr(decl, r);
    return method;
  }

  std::string getMethod(CXXConstructorDecl *decl, std::string recordFullyQualifiedName, std::string recordShortName) {
    std::string method;
    Rewriter r(SrcMgr, LangOpts);

    FunctionUpdater(Ctx, SrcMgr, LangOpts, r).TraverseDecl(decl);

    // out of class constructor decl format
    // ns:Class::Class(arg1, arg2...)
    std::string functionName = recordFullyQualifiedName + "::" + recordShortName;

    r.ReplaceText(decl->getNameInfo().getSourceRange(), functionName); // replace function name with fully qualified struct name + name
    method = getMethodStr(decl, r, "noexcept");
    return method;
  }

  bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
    if (!isCompressionCandidate(decl)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(decl, Ctx, SrcMgr, LangOpts, R);
    std::string compressedStructDef = compressionCodeGen.getCompressedStructDef();

    std::string methods; // generating methods
    for (auto *method : decl->methods()) {
      if (method->isImplicit()) continue;
      if (!method->isDefined() || !method->hasBody()) continue; // do not generate a method impl if it is not defined
      if (llvm::isa<CXXConstructorDecl>(method)) {
        auto *constr = llvm::cast<CXXConstructorDecl>(method);
        if (method->isImplicit()) continue; // skips autogenerated constructors
        if (method->isDefaulted()) continue; // skips default constructors
        if (constr->getNumParams() == 0) continue; // no-arg constructor is already provided
        methods += getMethod(constr, compressionCodeGen.getFullyQualifiedCompressedStructName(), compressionCodeGen.getCompressedStructName()) + "\n\n";
      } else {
        methods += getMethod(method, compressionCodeGen.getFullyQualifiedCompressedStructName()) + "\n\n";
      }
    }
    compressedStructDef += "\n" + methods;

    R.InsertTextAfterToken(decl->getEndLoc(), ";\n" + compressedStructDef);
    return true;
  }
};

class GlobalFunctionUpdater : public ASTConsumer, public RecursiveASTVisitor<GlobalFunctionUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit GlobalFunctionUpdater(ASTContext &Ctx,
                           SourceManager &SrcMgr,
                           LangOptions &LangOpts,
                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

public:

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitFunctionDecl(FunctionDecl *decl) {
    DeclContext *parent = decl->getParent();
    if (parent && parent->isRecord()) {
      RecordDecl *record = llvm::cast<RecordDecl>(parent);
      if (record && isCompressionCandidate(record)) return true;
    }
    return FunctionUpdater(Ctx, SrcMgr, LangOpts, R).TraverseDecl(decl);
  }
};

#endif // CLANG_COMPRESSIONREWRITERS_H
