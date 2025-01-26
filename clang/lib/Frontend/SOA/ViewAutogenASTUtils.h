#pragma once

#include <optional>

using namespace clang;

static Rewriter CreateRewriter(const Rewriter *oldR) {
  return Rewriter(oldR->getSourceMgr(), oldR->getLangOpts());
}

static Expr *IgnoreImplicitCasts(Expr *E) {
  if (!E || !llvm::isa<ImplicitCastExpr>(E)) return E;
  auto *implicitCast = llvm::cast<ImplicitCastExpr>(E);
  auto *base = implicitCast->IgnoreImpCasts();
  return base;
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

static QualType StripIndirections(QualType t) {
  while (true) {
    if (t->isPointerType()) {
      t = t->getPointeeType();
    } else if (t->isReferenceType()) t = t.getNonReferenceType();
    else break;
  }
  if (t.isConstQualified()) {
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
        auto newName = FD->getQualifiedNameAsString();
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
        auto newName = FD->getQualifiedNameAsString();
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
  return s;
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
      if (GetParent<BinaryOperator>(C, E)) usageKind = (UsageStats::UsageKind) (usageKind | UsageStats::UsageKind::Read);

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
        (*fs)[callee] = argCandidate;
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
  Rewriter *R;

  std::string getUniqueName(std::string name, Stmt *S) {
    return name + "_" + std::to_string((unsigned long) S);
  }

  template <typename T>
  T *getAttr(ASTContext &C, Stmt *S) {
    if (auto *AS = GetParent<AttributedStmt>(C, S, true)) {
      for (auto *attr : AS->getAttrs()) {
        if (llvm::isa<T>(attr)) return (T*) llvm::cast<T>(attr);
      }
    }
    return nullptr;
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

    auto *inc = llvm::cast<UnaryOperator>(S->getInc());
    if (inc->getOpcode() != UnaryOperatorKind::UO_PreInc && inc->getOpcode() != UnaryOperatorKind::UO_PostInc) {
      llvm::errs() << "For loop uses a non-standard increment\n";
      return "<unknown>";
    }

    auto cond = llvm::cast<BinaryOperator>(S->getCond());
    auto condRhs = cond->getRHS();
    auto condRhsString = R->getRewrittenText(condRhs->getSourceRange());
    return condRhsString;
  }

  std::string getReferenceViewDeclName(UsageStats &Stats, Stmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_view", S);
  }

  std::string getReferenceViewDecl(ASTContext &C, UsageStats &Stats, Stmt *S) {
    std::string view = "";
    std::string structName = getReferenceViewDeclName(Stats, S);
    view =+ "struct " + structName + " {\n";
    for (auto [F, v] : Stats.fields) {
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        view += type + " &" + name + " = *((" + type + "*) 1);\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        view += TypeToString(arrType->getElementType()) + " *" + F->getNameAsString() + ";\n";
      }
    }

    for (auto *method : Stats.methods) {
      view += TypeToString(method->getReturnType()) + " " + method->getNameAsString() + "(";
      for (auto *arg : method->parameters()) {
        view += TypeToString(arg->getType()) + " " + arg->getNameAsString() + ", ";
      }
      if (method->param_size() != 0) {
        view.pop_back();
        view.pop_back();
      }
      view += ") " + std::string(method->isConst() ? "const " : "");
      if (method->isStatic()) view += "{ return " + method->getQualifiedNameAsString() + "(); }\n";
      else view += R->getRewrittenText(method->getBody()->getSourceRange()) + "\n";
    }

    view += structName + " &operator[](int i) { return *this; }\n";
    view += structName + " &operator*() { return *this; }\n";
    view += "};\n";

    return view;
  }

  std::string getSoaHelperDeclName(UsageStats &Stats, Stmt *S) {
     return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper", S);
  }

  std::string getSoaHelperDecl(ASTContext &C, UsageStats &Stats, Stmt *S) {
    std::string soaHelperDecl = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    soaHelperDecl += "struct " + structName + " {\n";
    for (auto [F, usage] : Stats.fields) {
      auto name = F->getNameAsString();
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaHelperDecl += type + " *" + name + ";\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        soaHelperDecl += TypeToString(arrType->getElementType()) + " *" + name + ";\n";
      }
    }
    std::string viewName = getReferenceViewDeclName(Stats, S);
    soaHelperDecl += viewName + " operator[](int i) {\n";
    soaHelperDecl += "return {\n";
    for (auto [F, usage] : Stats.fields) {
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

  std::string getSoaBuffersDecl(std::string &sizeExprStr, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersDecl = "";
    for (auto [F, usage] : Stats.fields) {
      auto name = getUniqueName(F->getNameAsString(), S);
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaBuffersDecl += "alignas(64) " + type + " " + name + "[" + sizeExprStr + "];\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto type = TypeToString(arrType->getElementType());
        auto size = arrType->getSExtSize();
        soaBuffersDecl += "alignas(64) " + type + " " + name + + "[" + sizeExprStr + " * " + std::to_string(size) + "];\n";
      }
    }
    return soaBuffersDecl;
  }

  std::string getSoaBuffersInitForLoop(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersInit = "";
    soaBuffersInit += "for (int i = 0; i < " + sizeExprStr + "; i++) {\n";
    auto declName = D->getNameAsString();
    auto &Layout = D->getASTContext().getASTRecordLayout(Stats.record);

    for (auto [F, usage] : Stats.fields) {
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
      auto offset = std::to_string(Layout.getFieldOffset(idx) / 8);
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaBuffersInit += name + "[i] = " + "*((" + type + "*) (((char*) &" + declName + "[i]) + " + offset + "));\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto type = TypeToString(arrType->getElementType());
        auto size = arrType->getSExtSize();
        for (int i = 0; i < size; i++) {
          soaBuffersInit += name + "[i * " + std::to_string(size) + " + " + std::to_string(i) + "] = " + "*(((" + type + "*) (((char*) &" + declName + "[i]) + " + offset + ") " + std::to_string(i) + "));\n";
        }
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
    auto deref = std::string(target->getType()->isPointerType() ? "*" : "&");
    soaBuffersInit += "for (auto " + deref + itemName + " : " + container->getNameAsString() + ") {\n";
    auto &Layout = target->getASTContext().getASTRecordLayout(Stats.record);

    for (auto [F, usage] : Stats.fields) {
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
      auto offset = std::to_string(Layout.getFieldOffset(idx) / 8);
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaBuffersInit += name + "[" + counterName + "] = " + "*((" + type + "*) (((char*) &" + itemName + ") + " + offset + "));\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto type = TypeToString(arrType->getElementType());
        auto size = arrType->getSExtSize();
        for (int i = 0; i < size; i++) {
          soaBuffersInit += name + "[" + counterName + " * " + std::to_string(size) + " + " + std::to_string(i) + "] = " + "*(((" + type + "*) (((char*) &" + itemName + ") + " + offset + ") " + std::to_string(i) + "));\n";
        }
      }
    }
    soaBuffersInit += "++" + counterName + ";\n";
    soaBuffersInit += "}\n";
    return soaBuffersInit;
  }

  std::string getSoaHelperInstanceName(UsageStats &Stats, Stmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper_instance", S);
  }

  std::string getSoaHelperInstance(UsageStats &Stats, Stmt *S) {
    std::string soaHelperInstance = "";
    std::string structName = getSoaHelperDeclName(Stats, S);
    std::string instanceName = getSoaHelperInstanceName(Stats, S);
    soaHelperInstance += "auto " + instanceName + " = " + structName + "{";
    for (auto [F, usage] : Stats.fields) {
      auto name = getUniqueName(F->getNameAsString(), S);
      soaHelperInstance += name + ",";
    }
    soaHelperInstance += "};\n";
    return soaHelperInstance;
  }

  void rewriteForLoop(VarDecl *D, UsageStats &Stats, ForStmt *S) {
    auto iterVarName = llvm::cast<VarDecl>(llvm::cast<DeclStmt>(S->getInit())->getSingleDecl())->getNameAsString();
    std::string instanceName = getSoaHelperInstanceName(Stats, S);

    if (!llvm::isa<CompoundStmt>(S->getBody())) {
      llvm::errs() << "SOA: loop is not a compound stmt\n";
      exit(1);
    }

    auto bodyBegin = llvm::cast<CompoundStmt>(S->getBody())->getLBracLoc().getLocWithOffset(1);
    std::string source = "\n";
    source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + "];\n";
    R->InsertTextAfter(bodyBegin, source);
  }

  void rewriteForRangeLoop(VarDecl *D, std::string sizeExprStr, UsageStats &Stats, CXXForRangeStmt *S) {
    auto iterVarName = getUniqueName("iter", S);
    std::string instanceName = getSoaHelperInstanceName(Stats, S);

    if (!llvm::isa<CompoundStmt>(S->getBody())) {
      llvm::errs() << "SOA: loop is not a compound stmt\n";
      exit(1);
    }

    std::string source = "\n";
    source += "for (unsigned long " + iterVarName + " = 0; " + iterVarName + " < " + sizeExprStr + "; ++" + iterVarName + ") {\n";
    source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + "];\n";
    auto bodyBegin = llvm::cast<CompoundStmt>(S->getBody())->getLBracLoc().getLocWithOffset(1);
    auto bodyEnd = llvm::cast<CompoundStmt>(S->getBody())->getRBracLoc().getLocWithOffset(-1);
    source += R->getRewrittenText(SourceRange(bodyBegin, bodyEnd));
    source += "}\n";
    auto *attrStmt = GetParent<AttributedStmt>(D->getASTContext(), S, true);
    R->ReplaceText(attrStmt->getSourceRange(), source);
  }

  std::string getSoaBuffersWritebackForLoop(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, Stmt *S) {
    std::string soaBuffersWriteback = "";
    soaBuffersWriteback += "for (int i = 0; i < " + sizeExprStr + "; i++) {\n";
    auto declName = D->getNameAsString();
    auto &Layout = D->getASTContext().getASTRecordLayout(Stats.record);

    for (auto [F, usage] : Stats.fields) {
      if (!(usage & UsageStats::UsageKind::Write)) continue;

      auto name = getUniqueName(F->getNameAsString(), S);
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

      auto offset = std::to_string(Layout.getFieldOffset(idx) / 8);
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaBuffersWriteback += "*((" + type + "*) (((char*) &" + declName + "[i]) + " + offset + ")) = " + name + "[i];\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto type = TypeToString(arrType->getElementType());
        auto size = arrType->getSExtSize();
        for (int i = 0; i < size; i++) {
          soaBuffersWriteback += "*(((" + type + "*) (((char*) &" + declName + "[i]) + " + offset + ") " + std::to_string(i) + ")) = " + name + "[i * " + std::to_string(size) + " + " + std::to_string(i) + "];\n";
        }
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
    auto deref = std::string(target->getType()->isPointerType() ? "*" : "&");
    soaBuffersWriteback += "for (auto " + deref + itemName + " : " + container->getNameAsString() + ") {\n";
    auto &Layout = target->getASTContext().getASTRecordLayout(Stats.record);

    for (auto [F, usage] : Stats.fields) {
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
      auto offset = std::to_string(Layout.getFieldOffset(idx) / 8);
      if (!F->getType()->isArrayType()) {
        auto type = TypeToString(F->getType());
        soaBuffersWriteback += "*((" + type + "*) (((char*) &" + itemName + ") + " + offset + ")) = " + name + "[" + counterName + "];\n";
      } else {
        auto *arrType = llvm::cast<ConstantArrayType>(F->getType()->getAsArrayTypeUnsafe());
        auto type = TypeToString(arrType->getElementType());
        auto size = arrType->getSExtSize();
        for (int i = 0; i < size; i++) {
          soaBuffersWriteback += "*(((" + type + "*) (((char*) &" + itemName + ") + " + offset + ") " + std::to_string(i) + ")) = " + name + "[" + counterName + " * " + std::to_string(size) + " + " + std::to_string(i) + "];\n";
        }
      }
    }
    soaBuffersWriteback += "++" + counterName + ";\n";
    soaBuffersWriteback += "}\n";
    return soaBuffersWriteback;
  }

  template<typename Stmt>
  SourceLocation getPrologueLoc(ASTContext &C, Stmt *S) {
    auto *dataMovement = getAttr<SoaConversionDataMovementStrategyAttr>(C, S);
    auto defaultLoc =
        GetParent<AttributedStmt>(CI.getASTContext(), S)->getBeginLoc();
    if (!dataMovement)
      return defaultLoc;
    auto dataMovementKind = dataMovement->getDataMovementStrategy();
    switch (dataMovementKind) {
    case decltype(dataMovementKind)::InSitu:
      return defaultLoc;
    case decltype(dataMovementKind)::MoveToOuter: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getBeginLoc();
      if (parent)
        return parent->getBeginLoc();
      return defaultLoc;
    }
    case decltype(dataMovementKind)::MoveToOutermost: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *oldParent = parent;
      do {
        oldParent = parent;
        parent = GetParent<Stmt>(C, oldParent);
      } while (parent != nullptr);
      parent = oldParent;
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getBeginLoc();
      if (parent)
        return parent->getBeginLoc();
      return defaultLoc;
    }
    default: {
      llvm::errs() << "SOA: Unknown data movement strategy!\n";
      exit(1);
    }
    }
  }

  template<typename Stmt>
  SourceLocation getEpilogueLoc(ASTContext &C, Stmt *S) {
    auto *dataMovement = getAttr<SoaConversionDataMovementStrategyAttr>(C, S);
    auto defaultLoc =
        GetParent<AttributedStmt>(CI.getASTContext(), S)->getEndLoc();
    if (!dataMovement)
      return defaultLoc;
    auto dataMovementKind = dataMovement->getDataMovementStrategy();
    switch (dataMovementKind) {
    case decltype(dataMovementKind)::InSitu:
      return defaultLoc;
    case decltype(dataMovementKind)::MoveToOuter: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getEndLoc();
      if (parent)
        return parent->getEndLoc();
      return defaultLoc;
    }
    case decltype(dataMovementKind)::MoveToOutermost: {
      auto *parent = GetParent<Stmt>(C, S);
      auto *oldParent = parent;
      do {
        oldParent = parent;
        parent = GetParent<Stmt>(C, oldParent);
      } while (parent != nullptr);
      parent = oldParent;
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getEndLoc();
      if (parent)
        return parent->getEndLoc();
      return defaultLoc;
    }
    default: {
      llvm::errs() << "SOA: Unknown data movement strategy!\n";
      exit(1);
    }
    }
  }

  bool VisitForStmt(ForStmt *S) {
    auto *soaConversionTargetAttr = this->getAttr<SoaConversionTargetAttr>(CI.getASTContext(), S);
    if (!soaConversionTargetAttr) return true;

    auto *functionDecl = GetParent<FunctionDecl>(CI.getASTContext(), S);

    auto *targetDecl = getVarDeclByName(soaConversionTargetAttr->getTargetRef(), functionDecl);
    auto usageStats = UsageStats{};

    getUsageStats(&usageStats, targetDecl, S);

    auto sizeExprStr = getIterCountExprStr(S);
    auto soaBuffersDecl = getSoaBuffersDecl(sizeExprStr, usageStats, S);
    auto soaBuffersInit = getSoaBuffersInitForLoop(sizeExprStr, targetDecl, usageStats, S);
    auto refViewDecl = getReferenceViewDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperDecl = getSoaHelperDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperInstanceDecl = getSoaHelperInstance(usageStats, S);

    std::string prologue = "\n";
    prologue += "#pragma clang diagnostic push\n";
    prologue += "#pragma clang diagnostic ignored \"-Wvla-cxx-extension\"\n";
    prologue += soaBuffersDecl + "\n";
    prologue += soaBuffersInit + "\n";
    prologue += refViewDecl + "\n";
    prologue += soaHelperDecl + "\n";
    prologue += soaHelperInstanceDecl + "\n";

    auto prologueLoc = getPrologueLoc(CI.getASTContext(), S).getLocWithOffset(-1);
    R->InsertTextBefore(prologueLoc, prologue);

    rewriteForLoop(targetDecl, usageStats, S);

    auto soaWriteback = getSoaBuffersWritebackForLoop(sizeExprStr, targetDecl, usageStats, S);

    std::string epilogue = "\n";
    epilogue += soaWriteback + "\n";
    epilogue += "#pragma clang diagnostic pop\n";

    auto epilogueLoc = getEpilogueLoc(CI.getASTContext(), S).getLocWithOffset(1);
    R->InsertTextAfter(epilogueLoc, epilogue);

    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
    auto *soaConversionAttr = this->getAttr<SoaConversionAttr>(CI.getASTContext(), S);
    if (!soaConversionAttr) return true;

    auto *targetDecl = S->getLoopVariable();
    auto *containerDecl = llvm::cast<DeclRefExpr>(S->getRangeInit())->getDecl();

    auto usageStats = UsageStats{};
    getUsageStats(&usageStats, targetDecl, S);

    auto sizeExprStr = containerDecl->getNameAsString() + ".size()\n";
    auto soaBuffersDecl = getSoaBuffersDecl(sizeExprStr, usageStats, S);
    auto soaBuffersInit = getSoaBuffersInitForRangeLoop(sizeExprStr, targetDecl, containerDecl, usageStats, S);
    auto refViewDecl = getReferenceViewDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperDecl = getSoaHelperDecl(CI.getASTContext(), usageStats, S);
    auto soaHelperInstanceDecl = getSoaHelperInstance(usageStats, S);

    std::string prologue = "\n";
    prologue += "#pragma clang diagnostic push\n";
    prologue += "#pragma clang diagnostic ignored \"-Wvla-cxx-extension\"\n";
    prologue += soaBuffersDecl + "\n";
    prologue += soaBuffersInit + "\n";
    prologue += refViewDecl + "\n";
    prologue += soaHelperDecl + "\n";
    prologue += soaHelperInstanceDecl + "\n";

    auto prologueLoc = getPrologueLoc(CI.getASTContext(), S).getLocWithOffset(-1);
    R->InsertTextBefore(prologueLoc, prologue);

    rewriteForRangeLoop(targetDecl, sizeExprStr, usageStats, S);

    auto soaWriteback = getSoaBuffersWritebackForRangeLoop(sizeExprStr, targetDecl, containerDecl, usageStats, S);

    std::string epilogue = "\n";
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

  bool isTransformationCandidate(Decl *D) {
    struct IsTransformationCandidate : public RecursiveASTVisitor<IsTransformationCandidate> {
      bool isCandidate = false;

      bool VisitAttributedStmt(AttributedStmt *S) {
        for (auto *A : S->getAttrs()) {
          if (llvm::isa<SoaConversionAttr>(A) || llvm::isa<SoaConversionTargetAttr>(A)) {
            isCandidate = true;
            return false;
          }
        }
        return true;
      }

    } Finder{.isCandidate = false};
    Finder.TraverseDecl(D);
    auto isCandidate = Finder.isCandidate;
    return isCandidate;
  }

public:
  SoaConversionASTConsumer(CompilerInstance &CI) : CI(CI), R(&CI.getSourceManager().getRewriter()) {}

  bool VisitFunctionDecl(FunctionDecl *D) {
    if (D->isTemplated()) return true;

    auto isConversionCandidate = isTransformationCandidate(D);
    if (!isConversionCandidate) return true;

    SoaHandler{.CI = CI, .R = R}.TraverseDecl(D);
    return true;
  }

  bool VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
    auto isConversionCandidate = isTransformationCandidate(D);
    if (!isConversionCandidate) return true;

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

    std::string forwardDeclarations;
    std::string implicitSpecialisations;

    auto *origR = R;

    auto hasNonTypeTemplateArgs = false;
    for (auto *TemplateArgDecl : D->getTemplateParameters()->asArray()) {
      if (llvm::isa<NonTypeTemplateParmDecl>(TemplateArgDecl)) continue;
      hasNonTypeTemplateArgs = true;
      break;
    }

    for (auto *FD : D->specializations()) {
      auto newR = CreateRewriter(origR);
      this->R = &newR;

      VisitFunctionDecl(FD);
      InlineFunctionArgs(FD->getASTContext(), FD->getBody(), R);
      InlineTemplateParams(FD->getASTContext(), FD->getBody(), R);

      for (auto *Param : FD->parameters()) {
        if (Param->getType()->isFunctionType()) continue;
        auto typeSourceRange = Param->getTypeSourceInfo()->getTypeLoc().getSourceRange();
        auto typeStr = TypeToString(Param->getType());
        R->ReplaceText(typeSourceRange, typeStr);
      }

      if (hasNonTypeTemplateArgs) {
        auto newName = FD->getQualifiedNameAsString() + "_" + std::to_string((unsigned long) FD);
        auto nameSourceRange = FD->getNameInfo().getSourceRange();
        R->ReplaceText(nameSourceRange, newName);

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
        for (int paramIdx = 0; paramIdx < prevFD->getNumParams(); paramIdx++) {
          auto *Param = prevFD->getParamDecl(paramIdx);
          if (Param->getType()->isFunctionType()) continue;
          auto typeSourceRange = Param->getTypeSourceInfo()->getTypeLoc().getSourceRange();
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
