#pragma once

#include <optional>

using namespace clang;

static Expr *IgnoreImplicitCasts(Expr *E) {
  if (!E || !llvm::isa<ImplicitCastExpr>(E)) return E;
  auto *implicitCast = llvm::cast<ImplicitCastExpr>(E);
  auto *base = implicitCast->IgnoreImpCasts();
  return base;
}

template<typename T>
T *GetParent(ASTContext &C, DynTypedNode node) {
  for (auto parent: C.getParentMapContext().getParents(node)) {
    auto *exprMaybe = parent.get<T>();
    if (exprMaybe) return (T*) exprMaybe;
    exprMaybe = GetParent<T>(C, parent);
    if (exprMaybe) return (T*) exprMaybe;
  }
  return nullptr;
}

template<typename T, typename U>
T *GetParent(ASTContext &C, U *E) {
  auto dynNode = DynTypedNode::create(*E);
  auto *parentMaybe = GetParent<T>(C, dynNode);
  return parentMaybe;
}

template<typename E>
bool IsReadExpr(ASTContext &C, E *e) {
  auto dynNode = DynTypedNode::create(*e);
  while (true) {
    auto *implicitCast = GetParent<ImplicitCastExpr>(C, dynNode);
    if (!implicitCast) return false;
    if (implicitCast->getCastKind() == CastKind::CK_LValueToRValue) return true;
    dynNode = DynTypedNode::create(*implicitCast);
  }
}

QualType StripIndirections(QualType t) {
  while (true) {
    if (t->isPointerType()) {
      t = t->getPointeeType();
    } else if (t->isReferenceType()) t = t.getNonReferenceType();
    else return t;
  }
}

struct UsageStats {
  enum UsageKind {
    Unknown = 0, Read = 1,
    Write = 2,
  };

  CXXRecordDecl *record;
  std::map<FieldDecl*, UsageKind> fields;
  std::set<CXXMethodDecl*> methods;
  std::map<FunctionDecl *, int> leakyFunctions;
};

std::string CreateReferenceView(ASTContext &C, UsageStats *S) {
  std::string view = "";
  view =+ "struct " + S->record->getNameAsString() + "_view { \n";
  for (auto [k, v] : S->fields) {
    if (!k->getType()->isArrayType()) {
      view += k->getType().getAsString() + (k->getType()->isReferenceType() ? " " : " &") + k->getNameAsString() + ";\n";
    } else {
      auto *arrType = llvm::cast<ConstantArrayType>(k->getType()->getAsArrayTypeUnsafe());
      auto size = arrType->getSExtSize();
      view += arrType->getElementType().getAsString() + " *" + k->getNameAsString() + ";\n";
    }
  }

  auto &R = C.getSourceManager().getRewriter();
  for (auto *method : S->methods) {
    view += method->getReturnType().getAsString() + " " + method->getNameAsString() + "(";
    for (auto *arg : method->parameters()) {
      view += arg->getType().getAsString() + " " + arg->getNameAsString() + ", ";
    }
    if (method->param_size() != 0) {
      view.pop_back();
      view.pop_back();
    }
    view += ") " + R.getRewrittenText(method->getBody()->getSourceRange()) + "\n";
  }
  view += "};\n";

  return view;
}

struct UsageFinder : RecursiveASTVisitor<UsageFinder> {
  VarDecl *D;
  UsageStats *Stats;

  bool VisitMemberExpr(MemberExpr *E) {
    auto *memberDecl = E->getMemberDecl();
    auto *base = IgnoreImplicitCasts(E->getBase());

    auto DType = D->getType();
    CXXRecordDecl *DTypeDecl = nullptr;
    if (DType->isPointerType())
      DTypeDecl = DType->getPointeeType()->getAsCXXRecordDecl();
    else if (DType->isReferenceType())
      DTypeDecl = DType.getNonReferenceType()->getAsCXXRecordDecl();
    else if (DType->isRecordType())
      DTypeDecl = DType->getAsCXXRecordDecl();
    else {
      llvm::errs() << "SOA: Target type is not a record!\n";
      return true;
    }

    if (llvm::isa<CXXThisExpr>(base)) {
      auto *thisExpr = llvm::cast<CXXThisExpr>(base);
      auto *thisRecord = thisExpr->getBestDynamicClassType();
      if (thisRecord != DTypeDecl) return true;
    } else if (llvm::isa<DeclRefExpr>(base)) {
      auto *declRefBase = llvm::cast<DeclRefExpr>(base);
      if (declRefBase->getDecl() != D) return true;
    } else return true;

    auto &C = memberDecl->getASTContext();

    if (llvm::isa<CXXMethodDecl>(memberDecl)) {
      auto *methodDecl = llvm::cast<CXXMethodDecl>(memberDecl);
      this->Stats->methods.insert(methodDecl);
      return true;
    }

    if (llvm::isa<FieldDecl>(memberDecl)) {
      auto *fieldDecl = llvm::cast<FieldDecl>(memberDecl);

      auto isNew = this->Stats->fields.count(fieldDecl) == 0;
      if (isNew) this->Stats->fields[fieldDecl] = UsageStats::UsageKind::Unknown;

      auto isReadAccess = IsReadExpr(C, E);
      auto usageKind = isReadAccess ? UsageStats::UsageKind::Read : UsageStats::UsageKind::Write;

      // for functions that possibly return references, always assume that the value is at least read
      if (GetParent<ReturnStmt>(C, E)) usageKind = (UsageStats::UsageKind) (usageKind | UsageStats::UsageKind::Read);

      this->Stats->fields[fieldDecl] = (UsageStats::UsageKind) (this->Stats->fields[fieldDecl] | usageKind);
      return true;
    }

    return true;
  }
};

void FindLeakFunctions(VarDecl *D, Stmt *S, std::map<FunctionDecl *, int> &functions) {
  struct LeakFinder : RecursiveASTVisitor<LeakFinder> {
    VarDecl *D;
    std::map<FunctionDecl *, int> *fs;

    bool VisitCallExpr(CallExpr *E) {
      auto *callee = E->getDirectCallee();

      std::vector<int> argCandidates;
      for (int i = 0; i < callee->param_size(); i++) {
        auto *param = callee->getParamDecl(0);
        auto paramType = StripIndirections(param->getType());
        auto targetType = StripIndirections(D->getType());
        if (paramType.getAsString() == targetType.getAsString()) argCandidates.push_back(i);
      }

      if (argCandidates.empty()) return true;

      for (auto argCandidate : argCandidates) {
        struct DeclRefFinder : public RecursiveASTVisitor<DeclRefFinder> {
          VarDecl *D;
          bool found = false;

          bool VisitDeclRefExpr(DeclRefExpr *E) {
            if (E->getDecl() != D) return true;
            found = true;
            return false;
          }
        } finder{.D = D};
        auto *argExpr = E->getArg(argCandidate);
        finder.TraverseStmt(argExpr);
        if (!finder.found) continue;
        (*fs)[callee] = argCandidate;
        break;
      }

      return true;
    }
  };

  LeakFinder{.D = D, .fs = &functions}.TraverseStmt(S);
}

void FindUsages(VarDecl *D, UsageStats &stats, Stmt *S) {
  UsageFinder{.D = D, .Stats = &stats}.TraverseStmt(S);
  FindLeakFunctions(D, S, stats.leakyFunctions);

  while (true) {
    auto oldMethodCount = stats.methods.size();
    auto oldLeakyFunctionCount = stats.leakyFunctions.size();

    auto methodsCopy = stats.methods; // cant modify set while iterating, hence a copy is needed
    for (auto *method : methodsCopy) {
      UsageFinder{.D = D, .Stats = &stats}.TraverseCXXMethodDecl(method);
      FindLeakFunctions(D, method->getBody(), stats.leakyFunctions);
    }
    auto leakyFunctionsCopy = stats.leakyFunctions;
    for (auto [leakyFunction, arg] : leakyFunctionsCopy) {
      auto *D = leakyFunction->getParamDecl(arg);
      UsageFinder{.D = D, .Stats = &stats}.TraverseFunctionDecl(leakyFunction);
      FindLeakFunctions(D, leakyFunction->getBody(), stats.leakyFunctions);
    }
    auto haveStatsChanged = stats.methods.size() > oldMethodCount || stats.leakyFunctions.size() > oldLeakyFunctionCount;
    if (!haveStatsChanged) break;
  }
}

struct SoaHandler : public RecursiveASTVisitor<SoaHandler> {
  CompilerInstance &CI;
  Rewriter &R;

  template <typename T>
  T *getAttr(ASTContext &C, Stmt *S) {
    if (auto *AS = GetParent<AttributedStmt>(C, S)) {
      for (auto *attr : AS->getAttrs()) {
        if (llvm::isa<T>(attr)) return (T*) llvm::cast<T>(attr);
      }
    }
    return nullptr;
  }

  std::string getIterCountExprStr(ForStmt *S) {
    auto *init = llvm::cast<VarDecl>(llvm::cast<DeclStmt>(S->getInit())->getSingleDecl());
    Expr::EvalResult initVal;
    auto evalSuccess = init->getInit()->EvaluateAsInt(initVal, init->getASTContext());
    if (!evalSuccess) {
      llvm::errs() << "Cannot eval for loop counter init value\n";
      return "<unknown>";
    }
    auto initValInt = initVal.Val.getInt().getExtValue();
    if (initValInt != 0) {
      llvm::errs() << "For loop counter init value is not zero\n";
      return "unknown";
    }

    auto *inc = llvm::cast<UnaryOperator>(S->getInc());
    if (inc->getOpcode() != UnaryOperatorKind::UO_PreInc && inc->getOpcode() != UnaryOperatorKind::UO_PostInc) {
      llvm::errs() << "For loop uses a non-standard increment\n";
      return "<unknown>";
    }

    auto cond = llvm::cast<BinaryOperator>(S->getCond());
    auto condRhs = cond->getRHS();
    auto condRhsString = R.getRewrittenText(condRhs->getSourceRange());
    return condRhsString;
  }

  VarDecl *getVarDeclByName(llvm::StringRef name, Decl *ctx) {
    struct VarDeclFinder : RecursiveASTVisitor<VarDeclFinder> {
      llvm::StringRef name;
      VarDecl *found = nullptr;

      bool VisitVarDecl(VarDecl *D) {
        if (D->getName() != name) return true;
        found = D;
        return false;
      }
    } finder{.name = name};

    finder.TraverseDecl(ctx);
    auto *foundDecl = finder.found;
    return foundDecl;
  }

  void getUsageStats(UsageStats *Stats, VarDecl *D, Stmt *S) {
    CXXRecordDecl *decl;
    if (D->getType()->isPointerType()) {
      decl = D->getType()->getPointeeType()->getAsCXXRecordDecl();
    } else if (D->getType()->isReferenceType()) {
      decl = D->getType().getNonReferenceType()->getAsCXXRecordDecl();
    } else decl = D->getType()->getAsCXXRecordDecl();

    Stats->record = decl;
    FindUsages(D, *Stats, S);
  }

  bool VisitForStmt(ForStmt *S) {
    auto *soaConversionTargetAttr = this->getAttr<SoaConversionTargetAttr>(CI.getASTContext(), S);
    if (!soaConversionTargetAttr) return true;

    auto *functionDecl = GetParent<FunctionDecl>(CI.getASTContext(), S);

    auto *targetDecl = getVarDeclByName(soaConversionTargetAttr->getTargetRef(), functionDecl);
    auto usageStats = UsageStats{};
    getUsageStats(&usageStats, targetDecl, S);
    return true;
  }
};

struct SoaConversionASTConsumer : public ASTConsumer, public RecursiveASTVisitor<SoaConversionASTConsumer> {
  CompilerInstance &CI;
  Rewriter &R;

public:
  SoaConversionASTConsumer(CompilerInstance &CI) : CI(CI), R(CI.getSourceManager().getRewriter()) {}

  bool VisitFunctionDecl(FunctionDecl *D) {
    if (D->isTemplated()) return true;
    SoaHandler{.CI = CI, .R = R}.TraverseDecl(D);
    return true;
  }

  bool VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
    for (auto *FD : D->specializations()) {
      VisitFunctionDecl(FD);
    }
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }
};
