//
// Created by p on 16/08/22.
//

#ifndef CLANG_COMPRESSIONREWRITERS_H
#define CLANG_COMPRESSIONREWRITERS_H

#include "CompressionCodeGenResolver.h"

#define MARKER (std::string() + "/** " + __FILE__ + ":" + std::to_string(__LINE__) + "*/ ")

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

bool isAssignOpcode(BinaryOperatorKind kind) {
  switch (kind) {
  case BinaryOperatorKind::BO_Assign:
  case BinaryOperatorKind::BO_AddAssign:
  case BinaryOperatorKind::BO_RemAssign:
  case BinaryOperatorKind::BO_MulAssign:
  case BinaryOperatorKind::BO_DivAssign:
  case BinaryOperatorKind::BO_AndAssign:
  case BinaryOperatorKind::BO_OrAssign:
  case BinaryOperatorKind::BO_ShlAssign:
  case BinaryOperatorKind::BO_ShrAssign:
  case BinaryOperatorKind::BO_XorAssign:
    return true;
  default:
    return false;
  }
}

bool inLHSOfBinaryAssignOperator(ASTContext &Ctx, Expr *E) {
  const Expr *parent = E;
  while (const BinaryOperator *op = getParentNodeOfType<BinaryOperator>(Ctx, parent, ASTNodeKind::NodeKindId::NKI_BinaryOperator)) {
    if (isAssignOpcode(op->getOpcode()) && SubExprFinder().containsSubExpr(op->getLHS(), E)) return true;
    parent = op;
  }
  return false;
}

std::string templateArgumentToString(LangOptions &LangOpts, Rewriter &R, const TemplateArgument &arg) {
  typedef TemplateArgument::ArgKind Kind;
  switch (arg.getKind()) {
  case Kind::Type:  {
    auto argType = arg.getAsType();
    PrintingPolicy p(LangOpts);
    p.FullyQualifiedName = true;
    std::string type = argType.getAsString(p);
    return type;
  }
  case Kind::Integral: {
    auto argInt = arg.getAsIntegral();
    std::string argIntStr = toString(argInt, 10);
    return argIntStr;
  }
  case Kind::Expression: {
    auto *argExpr = arg.getAsExpr();
    std::string exprStr = R.getRewrittenText(argExpr->getSourceRange());
    return exprStr;
  }
  default: return "Template argument toString() to be implemented!";
  }
}

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, llvm::ArrayRef<TemplateArgument> argsList);
std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, const TemplateSpecializationType *templType);
std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, const ClassTemplateSpecializationDecl *templType);
std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, QualType origType);

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, llvm::ArrayRef<TemplateArgument> argsList) {

  std::string templateArgs = "";
  bool foundCompressionTemplateArg = false;
  for (auto &arg : argsList) {
    if (arg.getKind() != TemplateArgument::ArgKind::Type) {
      templateArgs += templateArgumentToString(LangOpts, R, arg) + ", "; continue;
    }

    std::string argPtrs = "";
    auto argAsType = getTypeFromIndirectType(arg.getAsType(), argPtrs);
    auto *recordDecl = argAsType->isRecordType() ? argAsType->getAsRecordDecl() : NULL;
    if (llvm::isa_and_nonnull<ClassTemplateSpecializationDecl>(recordDecl)) {
      std::string nestedTemplateArg = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, argAsType);
      if (!nestedTemplateArg.empty()) {
        foundCompressionTemplateArg = true;
        templateArgs += nestedTemplateArg;
      } else {
        templateArgs += templateArgumentToString(LangOpts, R, arg);
      }
    } else if (llvm::isa<TemplateSpecializationType>(argAsType)) {
      std::string newType = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, argAsType);
      if (newType.empty()) {
        templateArgs += templateArgumentToString(LangOpts, R, arg);
      } else {
        foundCompressionTemplateArg = true;
        templateArgs += newType;
      }
    } else if (isCompressionCandidate(recordDecl)) {
      foundCompressionTemplateArg = true;
      auto compressionCodeGen = CompressionCodeGenResolver(recordDecl, Ctx, SrcMgr, LangOpts, R);
      templateArgs += compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName();
    } else {
      templateArgs += templateArgumentToString(LangOpts, R, arg);
    }
    templateArgs += argPtrs + ", ";
  }

  if (!foundCompressionTemplateArg) return ""; // if no compression-related args were found, no need for rewriting

  if (templateArgs.length() > 0) {
    templateArgs.pop_back();
    templateArgs.pop_back(); // remove trailing ", "
  }

  return templateArgs;

}

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, const TemplateSpecializationType *templType) {
  return getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, templType->template_arguments());
}

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, const ClassTemplateSpecializationDecl *templType) {
  return getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, templType->getTemplateArgs().asArray());
}

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, QualType origType) {
  std::string ptrs = "";
  auto type = getTypeFromIndirectType(origType, ptrs);

  std::string templateArgs = "";
  std::string templateName = "";
  if (auto *templateType = type->getAs<TemplateSpecializationType>()) {
    templateArgs = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, templateType);
    templateName = templateType->getTemplateName().getAsTemplateDecl()->getQualifiedNameAsString();
  } else if (auto *classType = type->isRecordType() && llvm::isa<ClassTemplateSpecializationDecl>(type->getAsRecordDecl()) ? llvm::cast<ClassTemplateSpecializationDecl>(type->getAsRecordDecl()) : NULL) {
    templateArgs = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, classType);
    templateName = classType->getQualifiedNameAsString();
  }

  if (templateArgs.empty()) return "";

  std::string templateInstantiationType = templateName + "<" + templateArgs + ">" + ptrs;

  return templateInstantiationType;
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

    // sometimes the decl points to the definition
    // and then the 'static' attribute is missing
    while (functionDecl->getPreviousDecl()) {
      functionDecl = functionDecl->getPreviousDecl();
    }

    if (!functionDecl->isStatic()) return true;
    DeclContext *parentDecl = functionDecl->getParent();
    if (!parentDecl->isRecord()) return true;
    RecordDecl *recordDecl = llvm::cast<RecordDecl>(parentDecl);

    if (!isCompressionCandidate(recordDecl)) return true;
    SourceRange rangeToReplace = expr->getCallee()->getSourceRange();
    CompressionCodeGenResolver compressionCodeGenResolver = CompressionCodeGenResolver(recordDecl, Ctx, SrcMgr, LangOpts, R);
    std::string newSource = compressionCodeGenResolver.getGlobalNsFullyQualifiedCompressedStructName() + "::" + functionDecl->getNameAsString();
    R.ReplaceText(rangeToReplace, MARKER + newSource);
    return true;
  }

};

std::string getNewTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, QualType origType);

class ConstructorAndInitExprRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstructorAndInitExprRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

  template<typename X>
  struct Optional {
    bool some = false;
    X val;

  private:
    explicit Optional() {}

  public:

    static Optional<X> empty() {
      return Optional();
    }

    Optional(X val) {
      this->some = true;
      this->val = val;
    }

    bool isSome() {
      return this->some;
    }

    X getVal() {
      return this->val;
    }
  };

  Optional<DynTypedNode> getParentSkippingExprWithCleanups(const Expr *expr) {
    // Variable declaration by direct constructor invocation exist, e.g. Something__PACKED p(1,2);
    // This is called 'callinit'
    // In these cases, calculating type source range as the distance between the start of the declaration, and the start of the arguments
    // Simply does not work, as it 'swallows' the name that is in between
    auto parents = Ctx.getParents(*expr);
    if (parents.size() != 1) {
      llvm::errs() << "Multiple parents of CXXContstructorExpr\n";
      Optional<DynTypedNode>::empty();
    }

    auto parent = parents[0];
    auto parentNodeKind = parent.getNodeKind();

    // for reasons so obscure I don't even want to know,
    // sometimes constructor invocations are wrapped in an 'ExprWithCleanups' node
    if (parentNodeKind.KindId == ASTNodeKind::NKI_ExprWithCleanups) {
      parents = Ctx.getParents(*parent.get<ExprWithCleanups>());
      if (parents.size() != 1) {
        llvm::errs() << "Multiple parents of ExprWithCleanups\n";
        return Optional<DynTypedNode>::empty();
      }

      parent = parents[0];
    }

    return Optional<DynTypedNode>(parent);
  }

public:
  explicit ConstructorAndInitExprRewriter(ASTContext &Ctx,
                                          SourceManager &SrcMgr,
                                          LangOptions &LangOpts,
                                          Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  void updateTemplateInstantiationExpr(const CXXConstructExpr *expr) {
    auto *type = expr->getType()->getAs<TemplateSpecializationType>();
    if (!type) return;

    // sometimes, when variables whose type is a template instantiation are used as function arguments
    // a MAGICAL new CXXConstructorExpr appears in the AST in place of the function invocation (maybe if arg passed by value?)5
    // we should do nothing about it
    if (expr->getSourceRange().getBegin() == expr->getSourceRange().getEnd()) return;

    std::string newType = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, expr->getType());

    if (newType.empty()) return ;

    SourceLocation typeEnd;

    auto parentOpt = getParentSkippingExprWithCleanups(expr);
    if (!parentOpt.isSome()) return;
    auto parent = parentOpt.getVal();
    auto parentNodeKind = parent.getNodeKind();

    if (parentNodeKind.KindId == ASTNodeKind::NKI_VarDecl) {
      // I originally wrote this because of callinit variable initialization
      // but chances are that any constructor invocation that directly belongs to a var decl will be 'dealt with'
      // by the variable type rewriting itself
      auto *varDecl = parent.get<VarDecl>();

      if (varDecl->getInitStyle() == VarDecl::InitializationStyle::CallInit) return;
    }

    if (expr->getNumArgs() == 0) {
      typeEnd = expr->getEndLoc().getLocWithOffset(-2); // this removes the trailing '()' from the constructor invocation
    } else {
      auto *firstArg = expr->getArg(0);
      typeEnd = firstArg->getBeginLoc().getLocWithOffset(-2); // this removes the '(a' where a is the first letter of the argument name
    }

    auto sourceRange = SourceRange(expr->getBeginLoc(), typeEnd);
    if (sourceRange.getBegin() >= sourceRange.getEnd()) return ; // Sometimes simple function calls that involve std::vector get all the way to here, but the source range is invalid
    R.ReplaceText(sourceRange,  MARKER + newType);
  }


  // for some reason the VisitCXXConstructorExpr method below explicitly ignores zero-arg constructor invocations.
  // I can't remember why. Perhaps to avoid generating nested constructor invocations; there might have been an issue with multiple AST nodes being generated per invocation.
  // Anyway, we can't ignore the zero-arg invocations in 'new' constructor invocations, hence this method.
  bool VisitCXXNewExpr(CXXNewExpr *expr) {
    auto *constructorExpr = expr->getConstructExpr();
    if (!constructorExpr) return true;
    if (constructorExpr->getNumArgs() != 0) return true; // the VisitCXXConstructExpr method below will handle it


    // only visit array constructor exprs if new is called
    // otherwise other passes will change the type
    if (constructorExpr->getType()->isArrayType()) {
      return visitArrayTypedConstructorExpr(constructorExpr);
    }

    visitConstructorExpr(constructorExpr, false);

    return true;
  }


  // if an array of compressed type is created, we need to change the type in the 'new' invocation
  bool visitArrayTypedConstructorExpr(const CXXConstructExpr *expr) {
    auto *type = expr->getType()->getAsArrayTypeUnsafe();

    auto elementType = type->getElementType();

    if (!elementType->isRecordType()) return true;

    auto *elementDecl = elementType->getAsCXXRecordDecl();

    if (!isCompressionCandidate(elementDecl)) return true;

    auto beginLoc = expr->getBeginLoc();
    auto endLoc = expr->getBeginLoc().getLocWithOffset(elementDecl->getNameAsString().size()).getLocWithOffset(-1); // to preserve '[';
    auto range = SourceRange(beginLoc, endLoc);

    auto compressionCodeGen = CompressionCodeGenResolver(elementDecl, Ctx, SrcMgr, LangOpts, R);
    R.ReplaceText(range, MARKER + compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName());

    return true;
  }

  bool visitConstructorExpr(const CXXConstructExpr *expr, bool ignoreZeroArgCalls = true) {
    if (expr->isElidable()) return true;

    updateTemplateInstantiationExpr(expr);
    if (expr->getNumArgs() == 0 && ignoreZeroArgCalls) return true;
    if (expr->getConstructor()->isImplicit() && ignoreZeroArgCalls) return true;
    std::string ptrs;
    auto constructType = getTypeFromIndirectType(expr->getType(), ptrs);
    if (!constructType->isRecordType()) return true;
    auto *constructDecl = constructType->getAsRecordDecl();
    if (!isCompressionCandidate(constructDecl)) return true;

    // If this is a 'callinit' declaration, just changing the variable type
    // will do the trick
    auto parentOpt = getParentSkippingExprWithCleanups(expr);
    if (!parentOpt.isSome()) return true;
    auto parent = parentOpt.getVal();
    auto parentNodeKind = parent.getNodeKind();

    if (parentNodeKind.KindId == ASTNodeKind::NKI_VarDecl) {
      auto *varDecl = parent.get<VarDecl>();
      if (varDecl->getInitStyle() == VarDecl::InitializationStyle::CallInit) return true;
    }

    auto compressionCodeGen = CompressionCodeGenResolver(constructDecl, Ctx, SrcMgr, LangOpts, R);
    R.InsertTextBefore(expr->getBeginLoc(), MARKER + compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName() + "(");
    R.InsertTextAfterToken(expr->getEndLoc(), ")");
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *expr) {
    return visitConstructorExpr(expr);
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
    R.ReplaceText(initListExpr->getSourceRange(),  MARKER + "(" + source + ")");
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
    if (inLHSOfBinaryAssignOperator(Ctx, expr)) return true;

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
//SrcMgr.getSpellingLoc(expr->getBeginLoc()).dump(SrcMgr)
    std::string source = CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getGetterExpr(fieldDecl, varName);
    R.ReplaceText(SourceRange(expr->getBeginLoc(), expr->getEndLoc()),MARKER + source);

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
      R.ReplaceText(binaryOp->getSourceRange(), MARKER + source);
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

public:
  explicit ConstSizeArrReadAccessRewriter(ASTContext &Ctx,
                                          SourceManager &SrcMgr,
                                          LangOptions &LangOpts,
                                          Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
    if (getParentNodeOfType<ArraySubscriptExpr>(Ctx, e, ASTNodeKind::NodeKindId::NKI_ArraySubscriptExpr)) return true; // we're in the middle of a multi-dim arr ref
    if (getParentNodeOfType<ArrayInitLoopExpr>(Ctx, e, ASTNodeKind::NodeKindId::NKI_ArrayInitLoopExpr)) return true; // we're in the middle of an autogenerated constructor
    if (inLHSOfBinaryAssignOperator(Ctx, e)) return true;

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
      varName = "this->";
    } else {
      varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));
    }

    std::reverse(idxs.begin(), idxs.end());

    std::string source =
        CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getGetterExpr(fieldDecl, varName, idxs);

    R.ReplaceText(e->getSourceRange(), MARKER + source);
    return true;
  }
};

class ConstSizeArrWriteAccessRewriter : public ASTConsumer, public RecursiveASTVisitor<ConstSizeArrWriteAccessRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit ConstSizeArrWriteAccessRewriter(ASTContext &Ctx,
                                           SourceManager &SrcMgr,
                                           LangOptions &LangOpts,
                                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
    if (getParentNodeOfType<ArraySubscriptExpr>(Ctx, e, ASTNodeKind::NodeKindId::NKI_ArraySubscriptExpr)) return true; // we're in the middle of a multi-dim arr ref
    if (getParentNodeOfType<ArrayInitLoopExpr>(Ctx, e, ASTNodeKind::NodeKindId::NKI_ArrayInitLoopExpr)) return true; // we're in the middle of an autogenerated constructor
    if (!inLHSOfBinaryAssignOperator(Ctx, e)) return true;

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

    auto *binaryExpr = getParentNodeOfType<BinaryOperator>(Ctx, e, ASTNodeKind::NodeKindId::NKI_BinaryOperator);
    if (SubExprFinder::containsSubExpr(binaryExpr->getRHS(), e)) return true; // expression in RHS of the binary operator expr

    // Member expression 'var.field':
    //  - var is the base
    //  - field is the member
    std::string varName;
    Expr *baseExpr = memberExpr->getBase();
    if (llvm::isa<CXXThisExpr>(baseExpr)) {
      varName = "this->";
    } else {
      varName = R.getRewrittenText(SourceRange(memberExpr->getBeginLoc(), memberExpr->getMemberLoc().getLocWithOffset(-1)));
    }

    std::string val = R.getRewrittenText(binaryExpr->getRHS()->getSourceRange());

    std::reverse(idxs.begin(), idxs.end());

    std::string source =
        CompressionCodeGenResolver(fieldDecl->getParent(), Ctx, SrcMgr, LangOpts, R).getSetterExpr(fieldDecl, varName, idxs, val);

    R.ReplaceText(binaryExpr->getSourceRange(), MARKER + source);
    return true;
  }
};

class FunctionTemplateAtCallSiteUpdater : public ASTConsumer, public RecursiveASTVisitor<FunctionTemplateAtCallSiteUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

public:
  explicit FunctionTemplateAtCallSiteUpdater(ASTContext &Ctx, SourceManager &SrcMgr,
                           LangOptions &LangOpts, Rewriter &R)
      : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

public:

  bool VisitCallExpr(CallExpr *expr) {
    FunctionDecl *d = expr->getDirectCallee();
    if (!d || !d->isTemplateInstantiation() || !d->getTemplateSpecializationArgs()) return true;



    SourceLocation typeEnd;
    if (expr->getNumArgs() == 0) {
      typeEnd = expr->getEndLoc().getLocWithOffset(-2); // this removes the trailing '()' from the function invocation
    } else {
      Expr *firstArg = expr->getArg(0);
      typeEnd = firstArg->getBeginLoc().getLocWithOffset(-2); // this removes the '(a' where a is the first letter of the argument name
    }

    SourceRange range = SourceRange(expr->getBeginLoc(), typeEnd);
    if (range.getBegin() >= range.getEnd()) return true;

    // calls to std::vector methods such as .insert() are secretly template instantiations, even though no template arguments are present at call site
    // I couldn't figure out how to detect these 'implicit' template instantiations, so here we bluntly detect '<' and '>' chars as signs of explicit template instantiations
    std::string invocation = R.getRewrittenText(range);
    if (invocation.find('<') == std::string::npos || invocation.find('>') == std::string::npos) return true;

    // sometimes the range would eat the '(', esp. when the function is called like this
    // function(
    //    arg1,
    //    ...
    // );
    std::string potentiallyMissingParenthesis = "";
    if (invocation.find('(') != std::string::npos) potentiallyMissingParenthesis = "(";

    std::string templateArgs = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, d->getTemplateSpecializationArgs()->asArray());

    if (templateArgs.empty()) return true;

    std::string newType = "::" + d->getQualifiedNameAsString() + "<" + templateArgs + ">" + potentiallyMissingParenthesis;

    R.ReplaceText(range, MARKER + newType);

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
    FunctionTemplateAtCallSiteUpdater(Ctx, SrcMgr, LangOpts, R).TraverseStmt(stmt);
  }

};

void updateTemplateInstantiationType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, DeclaratorDecl *decl /** to cover VarDecl and FieldDecl */) {
  std::string newTemplateInstantiationType = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, decl->getType());

  if (newTemplateInstantiationType.empty()) return ;

  SourceRange typespecSourceRange = SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc());

  R.ReplaceText(typespecSourceRange, MARKER + newTemplateInstantiationType);
}

void updateConstSizeArrayType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, DeclaratorDecl *decl /** to cover VarDecl and FieldDecl */) {
  if (!decl->getType()->isConstantArrayType()) return ;
  auto *arrType = llvm::cast<ConstantArrayType>(decl->getType()->getAsArrayTypeUnsafe());

  std::vector<unsigned int> _dimensions;
  {
    auto *dimCountArrType = arrType;
    while (true) {
      _dimensions.push_back(dimCountArrType->getSize().getSExtValue());
      if (dimCountArrType->getElementType()->isConstantArrayType()) {
        dimCountArrType = llvm::cast<ConstantArrayType>(dimCountArrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        break;
      }
    }
  }

  QualType elementType;
  {
    auto *elementFindArrType = arrType;
    while (true) {
      if (elementFindArrType->getElementType()->isConstantArrayType()) {
        elementFindArrType = llvm::cast<ConstantArrayType>(elementFindArrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        elementType = elementFindArrType->getElementType();
        break;
      }
    }
  }

  if (!elementType->isRecordType()) return;

  auto *recordDecl = elementType->getAsRecordDecl();
  if (!isCompressionCandidate(recordDecl)) return;

  auto compressionCodeGen = CompressionCodeGenResolver(recordDecl, Ctx, SrcMgr, LangOpts, R);
  // here we know that the replaced var is a const-size arr,
  // and since for const-size arrs the type spec range covers whole 'type name[size]' declaration,
  // the type spec range is a suitable range for the replacement
  SourceRange sourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc());
  std::string compressedStructName = compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName();

  std::string completeDeclaration = compressedStructName + " " + decl->getNameAsString();
  for (auto &dim : _dimensions) {
    completeDeclaration += "[" + std::to_string(dim) + "]";
  }
  R.ReplaceText(sourceRange, MARKER + completeDeclaration);
}


void updateDeclType(Rewriter &R, DeclaratorDecl *decl, std::string typeStr) {
  // in normal cases, the decl goes like: 'type name;' e.g 'int i;'
  // in such cases, the TypeSpecStartLoc and TypeSpecEndLoc span only the type and not the name
  // HOWEVER
  // in cases of const-sized arrs, the size of the arr goes AFTER the name - 'type name[size];' e.g. int i[2];
  // and in such cases, the TypeSpecEndLoc points to the end of the size declaration which spans across the name as well

  SourceRange typeSourceRange = SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc());
  SourceRange nameSourceRange = SourceRange(decl->getLocation(), decl->getLocation().getLocWithOffset(decl->getNameAsString().length() -1));

  if (typeSourceRange.fullyContains(nameSourceRange)) {
    // TODO this will cause problems; this loses any [size] at the end, e.g. int a[2] becomes int a
    R.ReplaceText(typeSourceRange, MARKER + typeStr + " " + decl->getNameAsString());
  } else {
    R.ReplaceText(typeSourceRange, MARKER + typeStr);
  }
}

void updateVarType(ASTContext &Ctx, SourceManager &SrcMgr, LangOptions &LangOpts, Rewriter &R, DeclaratorDecl *decl) {
  std::string ptrs = "";
  auto type = getTypeFromIndirectType(decl->getType(), ptrs);
  updateTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, decl);
  updateConstSizeArrayType(Ctx, SrcMgr, LangOpts, R, decl);
  auto *record = type->getAsRecordDecl();
  if (!isCompressionCandidate(record)) return;
  auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
  updateDeclType(R, decl, compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
}

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

    std::string newTemplateType = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, returnType);
    if (!newTemplateType.empty()) {
      R.ReplaceText(decl->getReturnTypeSourceRange(), MARKER + newTemplateType);
      return true;
    }

    if (!returnType->isRecordType()) return true;
    auto *record = returnType->getAsRecordDecl();
    if (!isCompressionCandidate(record)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
    R.ReplaceText(decl->getReturnTypeSourceRange(), MARKER + compressionCodeGen.getFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
    return true;
  }

  bool VisitVarDecl(VarDecl *decl) { // changes local var types, incl. function args
    updateVarType(Ctx, SrcMgr, LangOpts, R, decl);
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
      R.InsertTextAfterToken(decl->getEndLoc(), MARKER + ";\n struct " + compressedStructName + ";\n");
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

  void handleBaseClasses(CXXRecordDecl *decl) {
    if (!decl->hasDefinition() || decl->getNumBases() == 0) return ;

    for (auto baseClass : decl->bases()) {
      auto type = baseClass.getType();
      auto newType = getNewTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, type);
      if (newType.empty()) continue ;
      auto range = baseClass.getTypeSourceInfo()->getTypeLoc().getSourceRange();
      R.ReplaceText(range, MARKER + newType);
    }
  }

  bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
    if (decl->getNameAsString() == "Y") {
      decl = decl;
    }
    handleBaseClasses(decl);
    if (!isCompressionCandidate(decl)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(decl, Ctx, SrcMgr, LangOpts, R);
    std::string compressedStructName = compressionCodeGen.getCompressedStructName();
    auto loc = decl->getBraceRange().getBegin();
    R.InsertTextAfterToken(loc, MARKER + "\n friend struct " + compressedStructName + ";\n");
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
    updateTemplateInstantiationType(Ctx, SrcMgr, LangOpts, R, decl);
    auto type = getTypeFromIndirectType(decl->getType(), ptrs);
    if (!type->isRecordType()) return true;
    auto *record = type->getAsRecordDecl();
    if (!isCompressionCandidate(record)) return true;
    auto compressionCodeGen = CompressionCodeGenResolver(record, Ctx, SrcMgr, LangOpts, R);
    updateDeclType(R, decl, compressionCodeGen.getGlobalNsFullyQualifiedCompressedStructName() + (ptrs.length() > 0 ? " " + ptrs : ""));
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
    R.InsertTextBefore(d->getBeginLoc(), MARKER + "#pragma pack(push, 1)\n");
    R.InsertTextAfterToken(d->getEndLoc(), MARKER + ";\n#pragma pack(pop)\n");
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


    // in method definitions, default values for arguments must be removed
    // i.e. they should only be present in method declarations
    // otherwise compilation fails
    // here, because we are using a 'scratch-pad' Rewriter
    // we can just remove the argument values from the Rewriter before proceeding
    for (auto *methodArg : decl->parameters()) {
      if (!methodArg->hasDefaultArg()) continue ;
      SourceLocation nameEndLoc = methodArg->getLocation().getLocWithOffset(methodArg->getNameAsString().size());
      SourceLocation endLoc = methodArg->getDefaultArgRange().getEnd();

      R.ReplaceText(SourceRange(nameEndLoc, endLoc), "");
    }

    std::string method;

//    SourceRange declRange = decl->getSourceRange(); // keywords like 'static' are also in this source range, and they are disallowed in function definitions
    SourceRange declRange;
    if (decl->getReturnTypeSourceRange().isValid()) {
      declRange = SourceRange(decl->getReturnTypeSourceRange().getBegin(), decl->getSourceRange().getEnd());
    } else {
      declRange = decl->getSourceRange(); // constructors don't have a valid return type source range, but we can use the entire source range here because constructors aren't static
    }
    SourceRange bodyRange = decl->getBody()->getSourceRange();


    if (declRange.fullyContains(bodyRange)) {
      method = R.getRewrittenText(declRange); // for when the body is provided in the h file in the declaration
    } else {
      // for when the decl and body are split into different (h and cpp) files
      method = R.getRewrittenText(declRange) // method signature
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
    method = getMethodStr(decl, r); // "noexcept");
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

    R.InsertTextAfterToken(decl->getEndLoc(), MARKER + ";\n" + compressedStructDef);
    return true;
  }
};

class GlobalFunctionAndVarUpdater : public ASTConsumer, public RecursiveASTVisitor<GlobalFunctionAndVarUpdater> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;
public:
  explicit GlobalFunctionAndVarUpdater(ASTContext &Ctx,
                           SourceManager &SrcMgr,
                           LangOptions &LangOpts,
                           Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

public:

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

  bool VisitFunctionDecl(FunctionDecl *decl) {
    if (decl->getNameAsString() == "method") {
      llvm::outs() << "method\n";
    }
    DeclContext *parent = decl->getParent();
    if (parent && parent->isRecord()) {
      RecordDecl *record = llvm::cast<RecordDecl>(parent);
      if (record && isCompressionCandidate(record)) return true;
    }
    return FunctionUpdater(Ctx, SrcMgr, LangOpts, R).TraverseDecl(decl);
  }

  bool VisitVarDecl(VarDecl *decl) {
    DeclContext *parent = decl->getParentFunctionOrMethod();
    if (parent) return true;
    updateVarType(Ctx, SrcMgr, LangOpts, R, decl);
    return true;
  }
};

#endif // CLANG_COMPRESSIONREWRITERS_H
