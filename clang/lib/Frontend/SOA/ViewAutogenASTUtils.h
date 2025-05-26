#pragma once

#include <optional>
#include <string>

using namespace clang;

static Rewriter CreateRewriter(const Rewriter *oldR) {
  return Rewriter(oldR->getSourceMgr(), oldR->getLangOpts());
}

// 'bool' in template args is replaced to '_Bool'
// which breaks compilation. this method reverses that
static std::string Replace_Bool(std::string s) {
  while (true) {
    auto pos = s.find("_Bool");
    if (pos == std::string::npos) break;
    s.replace(pos, 5, " bool");
  }
  return s;
}

static Expr *IgnoreImplicitCasts(Expr *E) {
  auto *oldE = E;
  do {
    oldE = E;
    E = E->IgnoreImpCasts();
    if (llvm::isa<MaterializeTemporaryExpr>(E)) E = llvm::cast<MaterializeTemporaryExpr>(E)->getSubExpr();
  } while (oldE != E);
  return E;
}

static Expr *IgnoreCommonIndirections(Expr *E) {
  while (true) {
    if (!E) return E;
    else if (llvm::isa<ImplicitCastExpr>(E)) E = llvm::cast<ImplicitCastExpr>(E)->IgnoreImpCasts();
    else if (llvm::isa<ParenExpr>(E)) E = llvm::cast<ParenExpr>(E)->IgnoreParens();
    else if (llvm::isa<UnaryOperator>(E)) E = llvm::cast<UnaryOperator>(E)->getSubExpr();
    else if (llvm::isa<ArraySubscriptExpr>(E)) E = llvm::cast<ArraySubscriptExpr>(E)->getBase();
    else return E;
  }
}

template<typename T>
static T *GetParent(ASTContext &C, DynTypedNode node, bool immediateOnly = false) {
  for (auto parent: C.getParentMapContext().getParents(node)) {
    auto *exprMaybe = parent.get<T>();
    if (exprMaybe) return (T*) exprMaybe;
    if (immediateOnly) return nullptr;
    exprMaybe = GetParent<T>(C, parent);
    if (exprMaybe) return (T*) exprMaybe;
  }
  return nullptr;
}

template<typename T, typename U>
static T *GetParent(ASTContext &C, U *E, bool immediateOnly = false) {
  auto dynNode = DynTypedNode::create(*E);
  auto *parentMaybe = GetParent<T>(C, dynNode, immediateOnly);
  return parentMaybe;
}

template <typename T>
T *GetAttr(ASTContext &C, Stmt *S) {
  if (auto *AS = GetParent<AttributedStmt>(C, S, true)) {
    for (auto *attr : AS->getAttrs()) {
      if (llvm::isa<T>(attr)) return (T*) llvm::cast<T>(attr);
    }
  }
  return nullptr;
}

static bool IsConversionCandidate(ASTContext &C, Stmt *S) {
  return GetAttr<SoaConversionAttr>(C, S)
         || GetAttr<SoaConversionTargetAttr>(C, S)
         || GetAttr<SoaConversionHoistAttr>(C, S)
         || GetAttr<SoaConversionOffloadComputeAttr>(C, S)
         || GetAttr<SoaConversionSimdAttr>(C, S);
}

static bool IsTransformationCandidate(ASTContext &C, Decl *D) {
  struct IsTransformationCandidate : public RecursiveASTVisitor<IsTransformationCandidate> {
    ASTContext &C;
    bool isCandidate = false;

    bool VisitForStmt(ForStmt *S) {
      if (!IsConversionCandidate(C, S)) return true;
      isCandidate = true;
      return false;
    }

    bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
      if (!IsConversionCandidate(C, S)) return true;
      isCandidate = true;
      return false;
    }

  } Finder{.C = C, .isCandidate = false};
  Finder.TraverseDecl(D);
  auto isCandidate = Finder.isCandidate;
  return isCandidate;
}

bool IsInOffloadingCtx(ASTContext &C, Stmt *S) {
  while (S) {
    if (GetAttr<SoaConversionOffloadComputeAttr>(C, S)) return true;
    S = GetParent<Stmt>(C, S);
  }
  return false;
}

template<typename E>
static bool IsReadExpr(ASTContext &C, E *e) {
  auto dynNode = DynTypedNode::create(*e);
  while (true) {
    auto *implicitCast = GetParent<ImplicitCastExpr>(C, dynNode);
    if (!implicitCast) break;
    if (implicitCast->getCastKind() == CastKind::CK_LValueToRValue) return true;
    dynNode = DynTypedNode::create(*implicitCast);
  }

  dynNode = DynTypedNode::create(*e);
  auto *implicitCast = GetParent<CXXConstructExpr>(C, dynNode);
  if (implicitCast) return true;
  return false;
}

static QualType StripIndirections(QualType t, bool stripConst = true) {
  while (true) {
    if (t->isPointerType()) {
      t = t->getPointeeType();
    } else if (t->isReferenceType()) t = t.getNonReferenceType();
    else break;
  }
  if (stripConst && t.isConstQualified()) {
    t.removeLocalConst();
  }
  return t;
}

static std::vector<CallExpr *> FindInvocations(FunctionDecl *D) {
  std::vector<CallExpr *> invocations;
  struct Finder : RecursiveASTVisitor<Finder> {
    FunctionDecl *FD;
    std::vector<CallExpr *> &v;

    bool VisitCallExpr(CallExpr *E) {
      auto *callee = E->getDirectCallee();
      if (!callee) return true;
      if (callee != FD) return true;
      v.push_back(E);
      return true;
    }
  } finder{.FD = D, .v = invocations};
  finder.TraverseDecl(D->getTranslationUnitDecl());
  return invocations;
}

static std::vector<Expr*> GetParmVals(ParmVarDecl *D) {
  std::vector<Expr*> vals;
  auto *FD = GetParent<FunctionDecl>(D->getASTContext(), D);
  auto paramIdx = -1;
  for (int i = 0; i < FD->getNumParams(); i++) {
    if (FD->getParamDecl(i) != D) continue;
    paramIdx = i;
    break;
  }
  if (paramIdx == -1) {
    llvm::errs() << "SOA: can't establish param idx\n";
    exit(1);
  }
  auto invocations = FindInvocations(FD);
  if (invocations.empty()) {
    llvm::errs() << "SOA: parent function is never called\n";
    exit(1);
  }

  for (auto *callExpr : invocations) {
    auto *argExpr = callExpr->getArg(paramIdx);
    vals.push_back(argExpr);
  }
  return vals;
}

static SourceRange GetFullTypeSourceRange(TypeLoc TL) {
  auto range = TL.getSourceRange();
  if (StripIndirections(TL.getType(), false).isConstQualified()) {
    range.setBegin(range.getBegin().getLocWithOffset(-6) /* 'const ' */);
  }
  return range;
}

static void InlineFunctionArgs(ASTContext &C, Stmt* S, Rewriter *R) {
  auto *FD = GetParent<FunctionDecl>(C, S);
  for (auto *Param : FD->parameters()) {
    auto pType = Param->getType();
    auto isFnOrFnPtr = pType->isFunctionType() || (pType->isPointerType() && pType->getPointeeType()->isFunctionType());
    if (!isFnOrFnPtr) continue;
    auto vals = GetParmVals(Param);
    if (vals.size() != 1) {
      llvm::errs() << "SOA: cannot inline function argument, multiple candidates\n";
      exit(1);
    }

    auto *fnDecl = llvm::cast<FunctionDecl>(llvm::cast<DeclRefExpr>(IgnoreImplicitCasts(vals[0]))->getDecl());

    struct Inliner : RecursiveASTVisitor<Inliner> {
      ParmVarDecl *D;
      FunctionDecl *FD;
      Rewriter *R;

      bool VisitDeclRefExpr(DeclRefExpr *E) {
        if (E->getDecl() != D) return true;
        auto newName = std::string("::") + FD->getQualifiedNameAsString();
        R->ReplaceText(E->getSourceRange(), newName);
        return true;
      }
    } inliner {.D = Param, .FD = fnDecl, .R = R};
    inliner.TraverseStmt(S);
  }
}

static void InlineTemplateParams(ASTContext &C, Stmt* S, Rewriter *R) {
  auto *FD = GetParent<FunctionDecl>(C, S);
  auto *D = FD->getPrimaryTemplate();
  if (!D) return;

  auto templateArgDefs = D->getTemplateParameters()->asArray();
  auto templateArgVals = FD->getTemplateSpecializationArgs()->asArray();

  for (int i = 0; i < templateArgDefs.size(); i++) {
    auto *templateArgDecl = templateArgDefs[i];
    auto templateArgVal = templateArgVals[i];

    if (templateArgVal.getKind() != clang::TemplateArgument::Declaration) continue;

    auto isFunction = templateArgVal.getAsDecl()->getType()->isFunctionType();
    if (!isFunction) continue;

    struct Inliner : RecursiveASTVisitor<Inliner> {
      NamedDecl *D;
      FunctionDecl *FD;
      Rewriter *R;

      bool VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E) {
        if (E->getParameter() != D) return true;
        auto newName = std::string("::") + FD->getQualifiedNameAsString();
        R->ReplaceText(E->getSourceRange(), newName);
        return true;
      }
    } inliner {.D = templateArgDecl, .FD = llvm::cast<FunctionDecl>(templateArgVal.getAsDecl()), .R = R};
    inliner.TraverseStmt(S);
  }
}

static std::string TypeToString(QualType type) {
  type = type.getCanonicalType();
  if (type->isBooleanType()) return "bool";
  auto s = type.getAsString();
  s = Replace_Bool(s);
  return s;
}

struct UsageStats {
  enum UsageKind {
    Unknown = 0, Read = 1,
    Write = 2,

    LLVM_MARK_AS_BITMASK_ENUM(2)
  };

  CXXRecordDecl *record;
  std::map<FieldDecl*, UsageKind> fields;
  std::set<CXXMethodDecl*> methods;
  std::map<FunctionDecl *, std::set<int>> leakyFunctions;

  int getSoaBufOffset(ASTContext &C, FieldDecl *D) {
    auto offsetBytes = 0;
    for (auto *F : D->getParent()->fields()) {
      if (!fields.count(F)) continue;
      if (D == F) return offsetBytes;
      offsetBytes += C.getTypeSize(F->getType()) / 8;
    }
    llvm::errs() << "SOA: field not found!";
    exit(1);
  }

  int getSoaBufOffsetOffload(ASTContext &C, FieldDecl *D) {
    auto offsetBytes = 0;
    for (auto *F : D->getParent()->fields()) {
      if (!fields.count(F)) continue;
      auto usage = fields[F];
      if (usage != UsageKind::Read) continue;
      if (D == F) return offsetBytes;
      offsetBytes += C.getTypeSize(F->getType()) / 8;
    }

    for (auto *F : D->getParent()->fields()) {
      if (!fields.count(F)) continue;
      auto usage = fields[F];
      if (usage != (UsageKind::Read | UsageKind::Write)) continue;
      if (D == F) return offsetBytes;
      offsetBytes += C.getTypeSize(F->getType()) / 8;
    }

    for (auto *F : D->getParent()->fields()) {
      if (!fields.count(F)) continue;
      auto usage = fields[F];
      if (usage != UsageKind::Write) continue;
      if (D == F) return offsetBytes;
      offsetBytes += C.getTypeSize(F->getType()) / 8;
    }

    llvm::errs() << "SOA: cannot determine field soa buf offset\n";
    exit(1);
  }
};

struct UsageFinder : RecursiveASTVisitor<UsageFinder> {
  VarDecl *D;
  UsageStats *Stats;

  bool VisitMemberExpr(MemberExpr *E) {
    auto *memberDecl = E->getMemberDecl();
    auto *base = IgnoreCommonIndirections(E->getBase());

    auto *DTypeDecl = StripIndirections(D->getType())->getAsCXXRecordDecl();

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
      if (GetParent<ReturnStmt>(C, E)) usageKind |= UsageStats::UsageKind::Read;
      if (GetParent<BinaryOperator>(C, E)) usageKind |= UsageStats::UsageKind::Read;

      this->Stats->fields[fieldDecl] |= usageKind;
      return true;
    }

    return true;
  }
};

void FindLeakFunctions(VarDecl *D, Stmt *S, std::map<FunctionDecl *, std::set<int>> &functions) {
  struct LeakFinder : RecursiveASTVisitor<LeakFinder> {
    VarDecl *D;
    std::map<FunctionDecl *, std::set<int>> *fs;

    void handleFunctionDecl(FunctionDecl *callee, CallExpr *E) {
      std::vector<int> argCandidates;
      for (int i = 0; i < callee->getNumParams(); i++) {
        auto *param = callee->getParamDecl(i);
        auto paramType = StripIndirections(param->getType());
        auto targetType = StripIndirections(D->getType());
        if (TypeToString(paramType) == TypeToString(targetType)) argCandidates.push_back(i);
      }

      if (argCandidates.empty()) return;

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
        (*fs)[callee].insert(argCandidate);
        break;
      }
    }

    void handleArgFunctions(CallExpr *E) {
      auto *Param = llvm::cast<ParmVarDecl>(E->getCalleeDecl());
      auto vals = GetParmVals(Param);
      if (vals.empty()) {
        llvm::errs() << "SOA: could not find arg functions\n";
        exit(1);
      }

      if (vals.size() > 1) {
        llvm::errs() << "SOA: found too many arg functions\n";
        exit(1);
      }

      auto *argExpr = IgnoreImplicitCasts(vals[0]);
      if (!llvm::isa<DeclRefExpr>(argExpr)) {
        llvm::errs() << "SOA: expected function argument, got something else\n";
        exit(1);
      }
      auto *argDecl = llvm::cast<DeclRefExpr>(argExpr);
      auto *argFnDecl = llvm::cast<FunctionDecl>(argDecl->getDecl());
      handleFunctionDecl(argFnDecl, E);

      return;
    }

    bool VisitCallExpr(CallExpr *E) {
      auto *callee = E->getDirectCallee();
      if (callee) {
        handleFunctionDecl(callee, E);
        return true;
      }

      auto *calleeDecl = E->getCalleeDecl();
      if (!llvm::isa<ParmVarDecl>(calleeDecl)) return true;
      handleArgFunctions(E);
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
    for (auto [leakyFunction, args] : leakyFunctionsCopy) {
      for (auto arg : args) {
        auto *D = leakyFunction->getParamDecl(arg);
        UsageFinder{.D = D, .Stats = &stats}.TraverseFunctionDecl(leakyFunction);
        FindLeakFunctions(D, leakyFunction->getBody(), stats.leakyFunctions);
      }
    }
    auto haveStatsChanged = stats.methods.size() > oldMethodCount || stats.leakyFunctions.size() > oldLeakyFunctionCount;
    if (!haveStatsChanged) break;
  }
}

struct SoaHandler : public RecursiveASTVisitor<SoaHandler> {
  CompilerInstance &CI;
  Rewriter *R;

  std::string getUniqueName(std::string name, Stmt *S) {
    return name + "_" + std::to_string((unsigned long) S);
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
    CXXRecordDecl *decl = StripIndirections(D->getType())->getAsCXXRecordDecl();
    Stats->record = decl;
    FindUsages(D, *Stats, S);
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

    auto *inc = llvm::cast<UnaryOperator>(IgnoreImplicitCasts(S->getInc()));
    if (inc->getOpcode() != UnaryOperatorKind::UO_PreInc && inc->getOpcode() != UnaryOperatorKind::UO_PostInc) {
      llvm::errs() << "For loop uses a non-standard increment\n";
      return "<unknown>";
    }

    auto cond = llvm::cast<BinaryOperator>(S->getCond());
    auto condRhs = cond->getRHS();
    auto condRhsString = R->getRewrittenText(condRhs->getSourceRange());
    return condRhsString;
  }

  std::string getIterCountExprStr(CXXForRangeStmt *S) {
    auto *containerDecl = llvm::cast<DeclRefExpr>(S->getRangeInit())->getDecl();
    auto sizeExprStr = containerDecl->getNameAsString() + "__size";
    return sizeExprStr;
  }

  std::string getReferenceViewDeclName(UsageStats &Stats, Stmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_view", S);
  }

  std::string getReferenceViewDecl(ASTContext &C, UsageStats &Stats, Stmt *S) {
    std::string view = "";
    std::string structName = getReferenceViewDeclName(Stats, S);
    view =+ "struct " + structName + " {\n";

    for (auto *decl : Stats.record->decls()) {
      if (llvm::isa<CXXRecordDecl>(decl)) {
        auto *recordDecl = llvm::cast<CXXRecordDecl>(decl);
        if (recordDecl->isImplicit()) continue;
        view += "using " + recordDecl->getNameAsString() + " = " + recordDecl->getQualifiedNameAsString() + ";\n";
      } else if (llvm::isa<EnumDecl>(decl)) {
        auto *enumDecl = llvm::cast<EnumDecl>(decl);
        view += "using " + enumDecl->getNameAsString() + " = " + enumDecl->getQualifiedNameAsString() + ";\n";
      } else if (llvm::isa<CXXMethodDecl>(decl)) {
        auto *method = llvm::cast<CXXMethodDecl>(decl);
        if (!method->isStatic()) continue;
        view += "static " + TypeToString(method->getReturnType()) + " " + method->getNameAsString() + "(";
        for (auto *arg : method->parameters()) {
          view += TypeToString(arg->getType()) + " " + arg->getNameAsString() + ", ";
        }
        if (method->param_size() != 0) {
          view.pop_back();
          view.pop_back();
        }
        view += ") { ";
        if (!method->getReturnType()->isVoidType()) view += "return ";
        view += method->getQualifiedNameAsString() + "(";
        for (auto *arg : method->parameters()) {
          view += arg->getNameAsString() + ", ";
        }
        if (method->param_size() != 0) {
          view.pop_back();
          view.pop_back();
        }
        view += "); }\n";
      }
    }

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        view += type + " & __restrict__ " + name + ";\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        view += TypeToString(arrType->getElementType()) + " *" + F->getNameAsString() + ";\n";
      }
    }

    for (auto *method : Stats.methods) {
      if (method->isStatic()) continue;
      view += TypeToString(method->getReturnType()) + " " + method->getNameAsString() + "(";
      for (auto *arg : method->parameters()) {
        view += TypeToString(arg->getType()) + " " + arg->getNameAsString() + ", ";
      }
      if (method->param_size() != 0) {
        view.pop_back();
        view.pop_back();
      }
      view += ") " + std::string(method->isConst() ? "const " : "");
      if (method->getBody()) {
        view += R->getRewrittenText(method->getBody()->getSourceRange()) + "\n";
      } else if (method->isConst()) {
        view += "{ /* the definition of this const method was unavailable during compilation. fingers crossed it did nothing important */ }\n";
      } else {
        llvm::errs() << "SOA: AoS function def is not available\n";
        exit(1);
      }
    }

    view += structName + " &operator[](int i) { return *this; }\n";
    view += structName + " &operator*() { return *this; }\n";
    view += "};\n";

    return view;
  }

  std::string getSoaHelperDeclName(UsageStats &Stats, Stmt *S) {
     return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper", S);
  }

  std::string getSoaHelperDeclClassic(ASTContext &C, UsageStats &Stats, Stmt *S) {
    std::string soaHelperDecl = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    soaHelperDecl += "struct " + structName + " {\n";
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaHelperDecl += type + " * __restrict__ " + name + ";\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        soaHelperDecl += TypeToString(arrType->getElementType()) + " * __restrict__ " + name + ";\n";
      }
    }
    std::string viewName = getReferenceViewDeclName(Stats, S);
    soaHelperDecl += viewName + " operator[](int i) {\n";
    soaHelperDecl += "return " + viewName + "{\n";
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        soaHelperDecl += name + "[i],\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto size = std::to_string(arrType->getSExtSize());
        soaHelperDecl += name + " + i * " + size + ",\n";
      }
    }
    soaHelperDecl += "};\n";
    soaHelperDecl += "}\n";
    soaHelperDecl += "};\n";
    return soaHelperDecl;
  }

  std::string getSoaHelperDeclOffload(ASTContext &C, UsageStats &Stats, Stmt *S) {
    std::string soaHelperDecl = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    soaHelperDecl += "struct " + structName + " {\n";
    soaHelperDecl += "char * __soa_buf_dev;\n";

    std::string viewName = getReferenceViewDeclName(Stats, S);
    soaHelperDecl += viewName + " operator[](int i, int size) {\n";
    soaHelperDecl += "return " + viewName + "{\n";
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        auto typeStr = TypeToString(F->getType());
        soaHelperDecl += "((" + typeStr + "*) (__soa_buf_dev + " + std::to_string(Stats.getSoaBufOffsetOffload(C, F)) + " * size))[i],\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto typeStr = TypeToString(arrType->getElementType());
        auto size = std::to_string(arrType->getSExtSize());
        soaHelperDecl += "((" + typeStr + "*) (__soa_buf_dev + " + std::to_string(Stats.getSoaBufOffsetOffload(C, F)) + " * size))[i * " + size + "],\n";
        soaHelperDecl += name + " + i * " + size + ",\n";
      }
    }
    soaHelperDecl += "};\n";
    soaHelperDecl += "}\n";
    soaHelperDecl += "};\n";
    return soaHelperDecl;
  }

  std::string getSoaHelperDecl(ASTContext &C, UsageStats &Stats, Stmt *S) {
    if (IsInOffloadingCtx(C, S)) {
      return getSoaHelperDeclOffload(C, Stats, S);
    }
    return getSoaHelperDeclClassic(C, Stats, S);
  }

  std::string getSoaBuffersDecl(std::string &sizeExprStr, ASTContext &C, UsageStats &Stats, Stmt *S) {
    unsigned int sizeInBytes = 0;
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      sizeInBytes += C.getTypeSize(F->getType()) / 8;
    }

    std::string soaBuffersDecl;

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = getUniqueName(F->getNameAsString(), S);
      auto offset = Stats.getSoaBufOffset(C, F);
      std::string type = "";
      if (!F->getType()->isArrayType()) {
        type = TypeToString(F->getType());
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        type = TypeToString(arrType->getElementType());
      }
      soaBuffersDecl += type + " " + name + "[" + sizeExprStr + "];\n";
    }
    return soaBuffersDecl;
  }

  std::string getSoaBuffersDeclOffload(std::string &sizeExprStr, ASTContext &C, UsageStats &Stats, Stmt *S) {
    unsigned int sizeInBytes = 0;
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      sizeInBytes += C.getTypeSize(F->getType()) / 8;
    }

    std::string soaBuffersDecl = "alignas (64) char " + getUniqueName("__soa_buf", S) + "[";
    soaBuffersDecl += std::to_string(sizeInBytes) + " * " + sizeExprStr + "];\n";

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = getUniqueName(F->getNameAsString(), S);
      auto offset = Stats.getSoaBufOffsetOffload(C, F);
      std::string type = "";
      if (!F->getType()->isArrayType()) {
        type = TypeToString(F->getType());
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        type = TypeToString(arrType->getElementType());
      }
      soaBuffersDecl += type + " * __restrict__ " + name + " = (" + type + "*) (" + getUniqueName("__soa_buf", S) + " + " + std::to_string(offset) + " * " + sizeExprStr + ");\n";
    }

    auto devBufName = getUniqueName("__soa_buf_dev", S);
    auto devBufSizeName = getUniqueName("__soa_buf_dev_size", S);

    std::string targetBuffers = "\n";
    targetBuffers += "static thread_local char *" + devBufName + " = nullptr;\n";
    targetBuffers += "static thread_local long " + devBufSizeName + " = 0;\n";
    targetBuffers += "if (" + sizeExprStr + " > " + devBufSizeName + ") [[unlikely]] {\n";
    targetBuffers += "omp_target_free(" + devBufName + ", omp_get_default_device());\n";
    targetBuffers += devBufName + " = (char*) omp_target_alloc(" + std::to_string(sizeInBytes) + " * " + sizeExprStr + ", omp_get_default_device());\n";
    targetBuffers += devBufSizeName + " = " + sizeExprStr + ";\n";
    targetBuffers += "}\n";

    soaBuffersDecl += targetBuffers;

    return soaBuffersDecl;
  }

  std::string getSoaBuffersInitForLoop(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersInit = "#pragma omp simd\n";
    soaBuffersInit += "for (int i = 0; i < " + sizeExprStr + "; i++) {\n";

    auto declName = D->getNameAsString();
    soaBuffersInit += "auto *rawPtr = (char*) &" + declName + "[i];\n";

    auto &Layout = D->getASTContext().getASTRecordLayout(Stats.record);

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto usage = Stats.fields[F];
      if (!(usage & UsageStats::UsageKind::Read)) continue;

      auto idx = -1;
      auto found = false;
      for (auto *field : Stats.record->fields()) {
        idx++;
        if (field != F) continue;
        found = true;
        break;
      }
      if (!found) {
        llvm::errs() << "SOA: soa field not found in struct\n";
        exit(1);
      }

      auto name = getUniqueName(F->getNameAsString(), S);
      auto sizeBytes = D->getASTContext().getTypeSize(F->getType()) / 8;
      auto size = std::to_string(sizeBytes);
      auto offsetBytes = Layout.getFieldOffset(idx) / 8;
      auto offset = std::to_string(offsetBytes);
      auto type = TypeToString(F->getType());

      if (sizeBytes > 8 && sizeBytes % 8 == 0) {
        // somehow copying more than 8 bytes this way can break vectorisation. go figure.
        // try to split transfers into multiple 8 byte transfers.
        int chunks = sizeBytes / 8;
        for (int chunkId = 0; chunkId < sizeBytes / 8; chunkId++) {
          soaBuffersInit += "std::memcpy((unsigned long*) &" + name + "[i] + " + std::to_string(chunkId) + ", (unsigned long*) &rawPtr[" + std::to_string(offsetBytes + chunkId * 8) + "], 8);\n";
        }
      } else {
        soaBuffersInit += "std::memcpy(&" + name + "[i], (" + type + "*) &rawPtr[" + offset + "], " + size + ");\n";
      }
    }

    soaBuffersInit += "}\n";
    return soaBuffersInit;
  }

  std::string getSoaBuffersInitForRangeLoop(std::string &sizeExprStr, VarDecl *target, ValueDecl *container, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersInit = "";
    std::string itemName = getUniqueName(target->getNameAsString(), S);
    std::string counterName = getUniqueName("__soa_init_counter", S);
    soaBuffersInit += "unsigned long " + counterName + " = 0;\n";
    auto isTargetTypePointer = target->getType()->isPointerType();
    auto deref = std::string(isTargetTypePointer ? "*" : "&");
    auto pointerArithmeticPrefix = std::string(isTargetTypePointer ? "" : "&");

    soaBuffersInit += "for (auto " + deref + itemName + " : " + container->getNameAsString() + ") {\n";
    soaBuffersInit += "auto *rawPtr = (char*) " + pointerArithmeticPrefix + itemName + ";\n";

    auto &Layout = target->getASTContext().getASTRecordLayout(Stats.record);

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto usage = Stats.fields[F];
      if (!(usage & UsageStats::UsageKind::Read)) continue;

      auto idx = -1;
      auto found = false;
      for (auto *field : Stats.record->fields()) {
        idx++;
        if (field != F) continue;
        found = true;
        break;
      }
      if (!found) {
        llvm::errs() << "SOA: soa field not found in struct\n";
        exit(1);
      }

      auto name = getUniqueName(F->getNameAsString(), S);
      auto sizeBytes = target->getASTContext().getTypeSize(F->getType()) / 8;
      auto size = std::to_string(sizeBytes);
      auto offsetBytes = Layout.getFieldOffset(idx) / 8;
      auto offset = std::to_string(offsetBytes);
      auto type = TypeToString(F->getType());

      if (sizeBytes > 8 && sizeBytes % 8 == 0) {
        // somehow copying more than 8 bytes this way can break vectorisation. go figure.
        // try to split transfers into multiple 8 byte transfers.
        int chunks = sizeBytes / 8;
        for (int chunkId = 0; chunkId < sizeBytes / 8; chunkId++) {
          soaBuffersInit += "std::memcpy((unsigned long*) &" + name + "[" + counterName + "] + " + std::to_string(chunkId) + ", (unsigned long*) &rawPtr[" + std::to_string(offsetBytes + chunkId * 8) + "], 8);\n";
        }
      } else {
        soaBuffersInit += "std::memcpy(&" + name + "[" + counterName + "], (" + type + "*) &rawPtr[" + offset + "], " + size + ");\n";
      }
    }
    soaBuffersInit += "++" + counterName + ";\n";
    soaBuffersInit += "}\n";
    return soaBuffersInit;
  }

  std::string getSoaHelperInstanceName(UsageStats &Stats, Stmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper_instance", S);
  }

  std::string getSoaHelperInstanceClassic(UsageStats &Stats, Stmt *S) {
    std::string soaHelperInstance = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    std::string instanceName = getSoaHelperInstanceName(Stats, S);
    soaHelperInstance += "auto " + instanceName + " = " + structName + "{";
    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto name = getUniqueName(F->getNameAsString(), S);
      soaHelperInstance += name + ",";
    }
    soaHelperInstance += "};\n";
    return soaHelperInstance;
  }

  std::string getSoaHelperInstanceOffload(UsageStats &Stats, Stmt *S) {
    std::string soaHelperInstance = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    std::string instanceName = getSoaHelperInstanceName(Stats, S);
    soaHelperInstance += "auto " + instanceName + " = " + structName + "{";
    soaHelperInstance += getUniqueName("__soa_buf_dev_alias", S);
    soaHelperInstance += "};\n";
    return soaHelperInstance;
  }

  std::string getSoaHelperInstance(ASTContext &C, UsageStats &Stats, Stmt *S) {
    if (IsInOffloadingCtx(C, S)) return getSoaHelperInstanceOffload(Stats, S);
    return getSoaHelperInstanceClassic(Stats, S);
  }

  std::vector<Stmt*> getConvertedNestedLoops(ASTContext &C, Stmt *S) {
    std::vector<Stmt*> nestedLoops;
    struct LoopFinder : RecursiveASTVisitor<LoopFinder> {
      Stmt *root;
      std::vector<Stmt*> *nestedLoops;
      ASTContext &C;

    public:
      bool VisitForStmt(ForStmt *S) {
        if (S == root) return true;
        auto *attr = GetAttr<SoaConversionTargetAttr>(C, S);
        if (!attr) return true;
        nestedLoops->push_back(S);
        return true;
      }

      bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
        if (S == root) return true;
        auto *attr = GetAttr<SoaConversionAttr>(C, S);
        if (!attr) return true;
        nestedLoops->push_back(S);
        return false;
      }
    } finder {.root = S, .nestedLoops = &nestedLoops, .C = C};
    finder.TraverseStmt(S);
    return nestedLoops;
  }

  std::string getOmpPrologue(ASTContext &C, std::string &sizeExprStr, UsageStats &Stats, Stmt *S) {
    std::string prologue = "";

    if (IsInOffloadingCtx(C, S)) {
      auto bytesToCopy = 0;
      for (auto *F : Stats.record->fields()) {
        if (!Stats.fields.count(F)) continue;
        auto usage = Stats.fields[F];
        if (!(usage & UsageStats::UsageKind::Read)) continue;
        bytesToCopy += C.getTypeSize(F->getType()) / 8;
      }
      prologue += "omp_target_memcpy(" + getUniqueName("__soa_buf_dev", S) + ", " + getUniqueName("__soa_buf", S) + ", " + std::to_string(bytesToCopy) + " * " + sizeExprStr + ", 0, 0, omp_get_default_device(), omp_get_initial_device());\n";
      // on Grace Hopper I get strange errors that thread-local is not supported for target
      // aliasing the thread_local buffer to a regular local variable is supposed to work around this
      prologue += "auto *" + getUniqueName("__soa_buf_dev_alias", S) + " = " + getUniqueName("__soa_buf_dev", S) + ";\n";
      return prologue;
    }
    return "";
  }

  std::string getOmpPragma(ASTContext &C, std::string &sizeExprStr, UsageStats &Stats, Stmt *S) {
    auto *offloadAttr = GetAttr<SoaConversionOffloadComputeAttr>(C, S);
    if (offloadAttr) {
      std::string pragma = "#pragma omp target teams distribute parallel for";
      pragma += " is_device_ptr(" + getUniqueName("__soa_buf_dev_alias", S) + ")";
      for (auto *nestedLoop : getConvertedNestedLoops(C, S)) {
        pragma += " is_device_ptr(" + getUniqueName("__soa_buf_dev_alias", nestedLoop) + ")";
      }
      return pragma;
    }
    auto *simdAttr = GetAttr<SoaConversionSimdAttr>(C, S);
    if (simdAttr) {
      return "#pragma omp simd";
    }
    return "";
  }

  void rewriteForLoop(VarDecl *D, std::string &sizeExprStr, UsageStats &Stats, ForStmt *S) {
    auto iterVarName = llvm::cast<VarDecl>(llvm::cast<DeclStmt>(S->getInit())->getSingleDecl())->getNameAsString();
    std::string instanceName = getSoaHelperInstanceName(Stats, S);

    if (!llvm::isa<CompoundStmt>(S->getBody())) {
      llvm::errs() << "SOA: loop is not a compound stmt\n";
      exit(1);
    }

    auto *attributedStmt = GetParent<AttributedStmt>(D->getASTContext(), S, true);
    if (!attributedStmt) {
      llvm::errs() << "SOA: cannot find loop attribute\n";
      exit(1);
    }

    auto pragma = "\n" + getOmpPragma(CI.getASTContext(), sizeExprStr, Stats, S) + "\n";
    R->InsertTextBefore(attributedStmt->getBeginLoc(), pragma);

    auto bodyBegin = llvm::cast<CompoundStmt>(S->getBody())->getLBracLoc().getLocWithOffset(1);
    std::string source = "\n";
    source += getSoaHelperInstance(D->getASTContext(), Stats, S);

    if (IsInOffloadingCtx(D->getASTContext(), S)) {
      source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + ", " + sizeExprStr + "];\n";
    } else {
      source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + "];\n";
    }
    R->InsertTextAfter(bodyBegin, source);
  }

  void rewriteForRangeLoop(VarDecl *D, std::string &sizeExprStr, UsageStats &Stats, CXXForRangeStmt *S) {
    auto iterVarName = getUniqueName("iter", S);
    std::string instanceName = getSoaHelperInstanceName(Stats, S);

    if (!llvm::isa<CompoundStmt>(S->getBody())) {
      llvm::errs() << "SOA: loop is not a compound stmt\n";
      exit(1);
    }

    std::string source = "\n" + getOmpPragma(CI.getASTContext(), sizeExprStr, Stats, S) + "\n";
    source += "for (unsigned long " + iterVarName + " = 0; " + iterVarName + " < " + sizeExprStr + "; ++" + iterVarName + ") {\n";
    source += getSoaHelperInstance(D->getASTContext(), Stats, S) + ";\n";

    if (IsInOffloadingCtx(D->getASTContext(), S)) {
      source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + ", " + sizeExprStr + "];\n";
    } else {
      source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + "];\n";
    }

    auto bodyBegin = llvm::cast<CompoundStmt>(S->getBody())->getLBracLoc().getLocWithOffset(1);
    auto bodyEnd = llvm::cast<CompoundStmt>(S->getBody())->getRBracLoc().getLocWithOffset(-1);
    source += R->getRewrittenText(SourceRange(bodyBegin, bodyEnd));
    source += "}\n";
    auto *attrStmt = GetParent<AttributedStmt>(D->getASTContext(), S, true);
    R->ReplaceText(attrStmt->getSourceRange(), source);
  }

  std::string getOmpEpilogue(ASTContext &C, std::string &sizeExprStr, UsageStats &Stats, Stmt *S) {
    std::string epilogue = "";

    if (IsInOffloadingCtx(C, S)) {
      auto offset = 0;
      auto bytesToCopy = 0;
      for (auto *F : Stats.record->fields()) {
        if (!Stats.fields.count(F)) continue;
        auto usage = Stats.fields[F];
        if (usage == UsageStats::UsageKind::Read) {
          offset += C.getTypeSize(F->getType()) / 8;
        }
        if (usage & UsageStats::UsageKind::Write) {
          bytesToCopy += C.getTypeSize(F->getType()) / 8;
        }
      }
      if (bytesToCopy == 0) {
        epilogue += "// no need to copy back from " + getUniqueName("__soa_buf_dev", S) + "\n";
      } else {
        epilogue += "omp_target_memcpy(" + getUniqueName("__soa_buf", S) + ", " + getUniqueName("__soa_buf_dev", S) + ", " + std::to_string(bytesToCopy) + " * " + sizeExprStr + ", " + std::to_string(offset) + " * " + sizeExprStr + ", " + std::to_string(offset) + " * " + sizeExprStr + ", omp_get_initial_device(), omp_get_default_device());\n";
      }
      return epilogue;
    }
    return "";
  }

  std::string getSoaBuffersWritebackForLoop(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersWriteback = "";
    auto declName = D->getNameAsString();
    auto &Layout = D->getASTContext().getASTRecordLayout(Stats.record);

    soaBuffersWriteback += "#pragma omp simd\n";
    soaBuffersWriteback += "for (int i = 0; i < " + sizeExprStr + "; i++) {\n";
    soaBuffersWriteback += "auto *rawPtr = (char*) &" + declName + "[i];\n";

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto usage = Stats.fields[F];
      if (!(usage & UsageStats::UsageKind::Write)) continue;

      auto idx = -1;
      auto found = false;
      for (auto *field : Stats.record->fields()) {
        idx++;
        if (field != F) continue;
        found = true;
        break;
      }
      if (!found) {
        llvm::errs() << "SOA: soa field not found in struct\n";
        exit(1);
      }

      auto name = getUniqueName(F->getNameAsString(), S);
      auto sizeBytes = D->getASTContext().getTypeSize(F->getType()) / 8;
      auto size = std::to_string(sizeBytes);
      auto offsetBytes = Layout.getFieldOffset(idx) / 8;
      auto offset = std::to_string(offsetBytes);
      auto type = TypeToString(F->getType());

      if (sizeBytes > 8 && sizeBytes % 8 == 0) {
        // somehow copying more than 8 bytes this way can break vectorisation. go figure.
        // try to split transfers into multiple 8 byte transfers.
        int chunks = sizeBytes / 8;
        for (int chunkId = 0; chunkId < sizeBytes / 8; chunkId++) {
          soaBuffersWriteback += "std::memcpy((unsigned long*) &rawPtr[" + std::to_string(offsetBytes + chunkId * 8) + "], (unsigned long*) &" + name + "[i] + " + std::to_string(chunkId) + ", 8);\n";
        }
      } else {
        soaBuffersWriteback += "std::memcpy((" + type + "*) &rawPtr[" + offset + "], &" + name + "[i], " + size + ");\n";
      }

    }
    soaBuffersWriteback += "}\n";
    return soaBuffersWriteback;
  }

  std::string getSoaBuffersWritebackForRangeLoop(std::string &sizeExprStr, VarDecl *target, ValueDecl *container, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersWriteback = "";
    std::string itemName = getUniqueName(target->getNameAsString(), S);
    std::string counterName = getUniqueName("__soa_writeback_counter", S);
    soaBuffersWriteback += "unsigned long " + counterName + " = 0;\n";
    auto isTargetTypePointer = target->getType()->isPointerType();
    auto deref = std::string(isTargetTypePointer ? "*" : "&");
    auto pointerArithmeticPrefix = std::string(isTargetTypePointer ? "" : "&");
    auto &Layout = target->getASTContext().getASTRecordLayout(Stats.record);

    soaBuffersWriteback += "#pragma omp simd\n";
    soaBuffersWriteback += "for (auto " + deref + itemName + " : " + container->getNameAsString() + ") {\n";
    soaBuffersWriteback += "auto *rawPtr = (char*) " + pointerArithmeticPrefix + itemName + ";\n";

    for (auto *F : Stats.record->fields()) {
      if (!Stats.fields.count(F)) continue;
      auto usage = Stats.fields[F];
      if (!(usage & UsageStats::UsageKind::Write)) continue;

      auto idx = -1;
      auto found = false;
      for (auto *field : Stats.record->fields()) {
        idx++;
        if (field != F) continue;
        found = true;
        break;
      }
      if (!found) {
        llvm::errs() << "SOA: soa field not found in struct\n";
        exit(1);
      }

      auto name = getUniqueName(F->getNameAsString(), S);
      auto sizeBytes = target->getASTContext().getTypeSize(F->getType()) / 8;
      auto size = std::to_string(sizeBytes);
      auto offsetBytes = Layout.getFieldOffset(idx) / 8;
      auto offset = std::to_string(offsetBytes);
      auto type = TypeToString(F->getType());

      if (sizeBytes > 8 && sizeBytes % 8 == 0) {
        // somehow copying more than 8 bytes this way can break vectorisation. go figure.
        // try to split transfers into multiple 8 byte transfers.
        int chunks = sizeBytes / 8;
        for (int chunkId = 0; chunkId < sizeBytes / 8; chunkId++) {
          soaBuffersWriteback += "std::memcpy((unsigned long*) &rawPtr[" + std::to_string(offsetBytes + chunkId * 8) + "], (unsigned long*) &" + name + "[" + counterName + "] + " + std::to_string(chunkId) + ", 8);\n";
        }
      } else {
        soaBuffersWriteback += "std::memcpy((" + type + "*) &rawPtr[" + offset + "], &" + name + "[" + counterName + "], " + size + ");\n";
      }
    }
    soaBuffersWriteback += "++" + counterName + ";\n";
    soaBuffersWriteback += "}\n";
    return soaBuffersWriteback;
  }

  template<typename Stmt>
  SourceLocation getPrologueDefsLoc(ASTContext &C, Stmt *S) {
    auto *FD = GetParent<FunctionDecl>(C, S);
    auto loc = llvm::cast<CompoundStmt>(FD->getBody())->getLBracLoc().getLocWithOffset(1);
    return loc;
  }

  template<typename Stmt>
  SourceLocation getPrologueLoc(ASTContext &C, Stmt *S) {
    auto *dataMovement = GetAttr<SoaConversionHoistAttr>(C, S);
    auto defaultLoc =
        GetParent<AttributedStmt>(CI.getASTContext(), S)->getBeginLoc();
    if (!dataMovement)
      return defaultLoc;
    auto hoistLevels = dataMovement->getLevels();
    switch (hoistLevels) {
    case 0:
      return defaultLoc;
    default: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *oldParent = parent;
      for (int i = 0; i < hoistLevels; i++) {
        oldParent = parent;
        parent = GetParent<Stmt>(C, oldParent);
      }
      parent = oldParent;
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getBeginLoc();
      if (parent)
        return parent->getBeginLoc();
      return defaultLoc;
    }
    }
  }

  template<typename Stmt>
  SourceLocation getEpilogueLoc(ASTContext &C, Stmt *S) {
    auto *dataMovement = GetAttr<SoaConversionHoistAttr>(C, S);
    auto defaultLoc =
        GetParent<AttributedStmt>(CI.getASTContext(), S)->getEndLoc();
    if (!dataMovement)
      return defaultLoc;
    auto hoistLevels = dataMovement->getLevels();
    switch (hoistLevels) {
    case 0:
      return defaultLoc;
    default: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *oldParent = parent;
      for (int i = 0; i < hoistLevels; i++) {
        oldParent = parent;
        parent = GetParent<Stmt>(C, oldParent);
      }
      parent = oldParent;
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getEndLoc();
      if (parent)
        return parent->getEndLoc();
      return defaultLoc;
    }
    }
  }

  bool VisitForStmt(ForStmt *S) {
    if (!IsConversionCandidate(CI.getASTContext(), S)) return true;

    auto *soaConversionTargetAttr = GetAttr<SoaConversionTargetAttr>(CI.getASTContext(), S);

    auto *functionDecl = GetParent<FunctionDecl>(CI.getASTContext(), S);

    auto *targetDecl = getVarDeclByName(soaConversionTargetAttr->getTargetRef(), functionDecl);
    auto usageStats = UsageStats{};

    getUsageStats(&usageStats, targetDecl, S);

    auto sizeExprStr = getIterCountExprStr(S);
    auto soaBuffersDecl = IsInOffloadingCtx(CI.getASTContext(), S)
        ? getSoaBuffersDeclOffload(sizeExprStr, CI.getASTContext(), usageStats, S)
        : getSoaBuffersDecl(sizeExprStr, CI.getASTContext(), usageStats, S);
    auto soaBuffersInit = getSoaBuffersInitForLoop(sizeExprStr, targetDecl, usageStats, S);
    auto refViewDecl = getReferenceViewDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperDecl = getSoaHelperDecl(CI.getASTContext(), usageStats, S);
    auto ompPrologue = getOmpPrologue(CI.getASTContext(), sizeExprStr, usageStats, S);

    std::string prologueDefs = "\n";
    prologueDefs += "#pragma clang diagnostic push\n";
    prologueDefs += "#pragma clang diagnostic ignored \"-Wvla-cxx-extension\"\n\n";
    prologueDefs += refViewDecl + "\n";
    prologueDefs += soaHelperDecl + "\n";

    auto prologueDefsLoc = getPrologueDefsLoc(CI.getASTContext(), S);
    R->InsertTextBefore(prologueDefsLoc, prologueDefs);

    std::string prologue = "\n";
    prologue += soaBuffersDecl + "\n";
    prologue += soaBuffersInit + "\n";
    prologue += ompPrologue + "\n";

    auto prologueLoc = getPrologueLoc(CI.getASTContext(), S).getLocWithOffset(-1);
    R->InsertTextBefore(prologueLoc, prologue);

    rewriteForLoop(targetDecl, sizeExprStr, usageStats, S);

    auto ompEpilogue = getOmpEpilogue(CI.getASTContext(), sizeExprStr, usageStats, S);
    auto soaWriteback = getSoaBuffersWritebackForLoop(sizeExprStr, targetDecl, usageStats, S);

    std::string epilogue = "\n";
    epilogue += ompEpilogue + "\n";
    epilogue += soaWriteback + "\n";
    epilogue += "#pragma clang diagnostic pop\n";

    auto epilogueLoc = getEpilogueLoc(CI.getASTContext(), S).getLocWithOffset(1);
    R->InsertTextAfter(epilogueLoc, epilogue);

    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
    if (!IsConversionCandidate(CI.getASTContext(), S)) return true;

    auto *targetDecl = S->getLoopVariable();
    auto *containerDecl = llvm::cast<DeclRefExpr>(S->getRangeInit())->getDecl();

    auto usageStats = UsageStats{};
    getUsageStats(&usageStats, targetDecl, S);

    auto sizeExprStr = getIterCountExprStr(S);
    auto sizeDeclStmt = std::string("auto ") + sizeExprStr + " = " + containerDecl->getNameAsString() + ".size();";
    auto soaBuffersDecl = IsInOffloadingCtx(CI.getASTContext(), S)
                              ? getSoaBuffersDeclOffload(sizeExprStr, CI.getASTContext(), usageStats, S)
                              : getSoaBuffersDecl(sizeExprStr, CI.getASTContext(), usageStats, S);
    auto soaBuffersInit = getSoaBuffersInitForRangeLoop(sizeExprStr, targetDecl, containerDecl, usageStats, S);
    auto refViewDecl = getReferenceViewDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperDecl = getSoaHelperDecl(CI.getASTContext(), usageStats, S);
    auto ompPrologue = getOmpPrologue(CI.getASTContext(), sizeExprStr, usageStats, S);

    std::string prologueDefs = "\n";
    prologueDefs += "#pragma clang diagnostic push\n";
    prologueDefs += "#pragma clang diagnostic ignored \"-Wvla-cxx-extension\"\n\n";
    prologueDefs += refViewDecl + "\n";
    prologueDefs += soaHelperDecl + "\n";

    auto prologueDefsLoc = getPrologueDefsLoc(CI.getASTContext(), S);
    R->InsertTextBefore(prologueDefsLoc, prologueDefs);

    std::string prologue = "\n";
    prologue += sizeDeclStmt + "\n";
    prologue += soaBuffersDecl + "\n";
    prologue += soaBuffersInit + "\n";
    prologue += ompPrologue + "\n";

    auto prologueLoc = getPrologueLoc(CI.getASTContext(), S).getLocWithOffset(-1);
    R->InsertTextBefore(prologueLoc, prologue);

    rewriteForRangeLoop(targetDecl, sizeExprStr, usageStats, S);

    auto ompEpilogue = getOmpEpilogue(CI.getASTContext(), sizeExprStr, usageStats, S);
    auto soaWriteback = getSoaBuffersWritebackForRangeLoop(sizeExprStr, targetDecl, containerDecl, usageStats, S);

    std::string epilogue = "\n";
    epilogue += ompEpilogue + "\n";
    epilogue += soaWriteback + "\n";
    epilogue += "#pragma clang diagnostic pop\n";

    auto epilogueLoc = getEpilogueLoc(CI.getASTContext(), S).getLocWithOffset(1);
    R->InsertTextAfter(epilogueLoc, epilogue);

    return true;
  }
};

struct SoaConversionASTConsumer : public ASTConsumer, public RecursiveASTVisitor<SoaConversionASTConsumer> {
  CompilerInstance &CI;
  Rewriter *R;

public:
  SoaConversionASTConsumer(CompilerInstance &CI) : CI(CI), R(&CI.getSourceManager().getRewriter()) {}

  bool VisitFunctionDecl(FunctionDecl *D) {
    if (D->isTemplated()) return true;
    if (!IsTransformationCandidate(CI.getASTContext(), D)) return true;

    SoaHandler{.CI = CI, .R = R}.TraverseDecl(D);
    return true;
  }

  bool VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
    if (!IsTransformationCandidate(CI.getASTContext(), D)) return true;

    int specCount = 0;
    for (auto *FD : D->specializations()) specCount++;

    if (specCount == 0) return true;
    if (specCount == 1) {
      for (auto *FD : D->specializations()) {
        VisitFunctionDecl(FD);
        InlineFunctionArgs(FD->getASTContext(), FD->getBody(), R);
        InlineTemplateParams(FD->getASTContext(), FD->getBody(), R);
      }
      return true;
    }

    auto origSourceRange = D->getSourceRange();
    if (origSourceRange.getBegin().isInvalid()) {
      // templates with only auto args do not have a valid begin loc
      // likely because the begin loc is defined as the loc of the
      // template keyword. in this case, treat the type loc as begin loc
      origSourceRange.setBegin(D->getTemplatedDecl()->getSourceRange().getBegin());
    }

    auto *previousDecl = D->getPreviousDecl();
    SourceRange origPrevSourceRange;
    if (previousDecl) {
      origPrevSourceRange = previousDecl->getSourceRange();
      if (origPrevSourceRange.getBegin().isInvalid()) {
        // templates with only auto args do not have a valid begin loc
        // likely because the begin loc is defined as the loc of the
        // template keyword. in this case, treat the type loc as begin loc
        origPrevSourceRange.setBegin(previousDecl->getTemplatedDecl()->getSourceRange().getBegin());
      }
    }

    std::string forwardDeclarations = "\n";
    std::string implicitSpecialisations = "\n";

    auto *origR = R;

    auto hasNonTypeTemplateArgs = false;
    for (auto *TemplateArgDecl : D->getTemplateParameters()->asArray()) {
      if (llvm::isa<NonTypeTemplateParmDecl>(TemplateArgDecl)) continue;
      hasNonTypeTemplateArgs = true;
      break;
    }

    for (auto *FD : D->specializations()) {
      if (!FD->hasBody()) continue;

      auto newR = CreateRewriter(origR);
      this->R = &newR;

      VisitFunctionDecl(FD);
      InlineFunctionArgs(FD->getASTContext(), FD->getBody(), R);
      InlineTemplateParams(FD->getASTContext(), FD->getBody(), R);

      for (auto *Param : FD->parameters()) {
        if (Param->getType()->isFunctionType()) continue;
        auto typeSourceRange = GetFullTypeSourceRange(Param->getTypeSourceInfo()->getTypeLoc());
        auto typeStr = TypeToString(Param->getType());
        R->ReplaceText(typeSourceRange, typeStr);
      }

      if (hasNonTypeTemplateArgs) {
        auto newName = std::string("::") + FD->getQualifiedNameAsString() + "_" + std::to_string((unsigned long) FD);
        auto nameSourceRange = FD->getNameInfo().getSourceRange();
        R->ReplaceText(nameSourceRange, FD->getNameAsString() + "_" + std::to_string((unsigned long) FD));

        struct CallRewriter : RecursiveASTVisitor<CallRewriter> {
          FunctionDecl *D;
          Rewriter *R;
          std::string &newName;

          bool VisitCallExpr(CallExpr *E) {
            if (E->getCalleeDecl() != D) return true;
            auto *declRefExpr = llvm::cast<DeclRefExpr>(IgnoreImplicitCasts(E->getCallee()));
            auto declRefExprString = R->getRewrittenText(declRefExpr->getSourceRange());

            auto nameSourceRange = declRefExpr->getSourceRange();

            auto sRef = llvm::StringRef(declRefExprString);
            auto lPos = sRef.find('<');
            if (lPos != std::string::npos) {
              nameSourceRange.setEnd(nameSourceRange.getBegin().getLocWithOffset(lPos).getLocWithOffset(-1));
            }

            R->ReplaceText(nameSourceRange, newName);
            return true;
          }
        } CallRewriter{.D = FD, .R = origR, .newName = newName};
        CallRewriter.TraverseDecl(D->getTranslationUnitDecl());
      }

      auto modifiedTemplate = R->getRewrittenText(origSourceRange);
      if (!FD->getTemplateSpecializationInfo()->isExplicitInstantiationOrSpecialization()) {
        implicitSpecialisations += modifiedTemplate + "\n";
      } else {
        llvm::errs() << "SOA: Explicit instantiations are not yet supported\n";
        exit(1);
      }

      // if the template has an explicit declaration, we must do the same
      if (previousDecl) {
        auto *prevFD = previousDecl->getAsFunction();

        auto nameSourceRange = prevFD->getNameInfo().getSourceRange();
        R->ReplaceText(nameSourceRange, prevFD->getNameAsString() + "_" + std::to_string((unsigned long) FD));

        for (int paramIdx = 0; paramIdx < prevFD->getNumParams(); paramIdx++) {
          auto *Param = prevFD->getParamDecl(paramIdx);
          if (Param->getType()->isFunctionType()) continue;
          auto typeSourceRange = GetFullTypeSourceRange(Param->getTypeSourceInfo()->getTypeLoc());
          auto typeStr = TypeToString(FD->getParamDecl(paramIdx)->getType());
          R->ReplaceText(typeSourceRange, typeStr);
        }

        auto modifiedForwardDecl = R->getRewrittenText(origPrevSourceRange);
        forwardDeclarations += modifiedForwardDecl + ";\n";
      }
    }

    this->R = origR;

    R->InsertTextBefore(origSourceRange.getBegin().getLocWithOffset(-1), implicitSpecialisations);
    if (previousDecl) {
      R->InsertTextBefore(origPrevSourceRange.getBegin().getLocWithOffset(-1), forwardDeclarations);
    }
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }
};
