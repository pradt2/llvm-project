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
T *GetParent(ASTContext &C, DynTypedNode node, bool immediateOnly = false) {
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
T *GetParent(ASTContext &C, U *E, bool immediateOnly = false) {
  auto dynNode = DynTypedNode::create(*E);
  auto *parentMaybe = GetParent<T>(C, dynNode, immediateOnly);
  return parentMaybe;
}

template<typename E>
bool IsReadExpr(ASTContext &C, E *e) {
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
    else return t;
  }
}

static std::string TypeToString(QualType type) {
  auto s = type.getCanonicalType().getAsString();
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
        if (TypeToString(paramType) == TypeToString(targetType)) argCandidates.push_back(i);
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

  std::string getUniqueName(std::string name, ForStmt *S) {
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
    CXXRecordDecl *decl;
    if (D->getType()->isPointerType()) {
      decl = D->getType()->getPointeeType()->getAsCXXRecordDecl();
    } else if (D->getType()->isReferenceType()) {
      decl = D->getType().getNonReferenceType()->getAsCXXRecordDecl();
    } else decl = D->getType()->getAsCXXRecordDecl();

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
    auto condRhsString = R.getRewrittenText(condRhs->getSourceRange());
    return condRhsString;
  }

  std::string getReferenceViewDeclName(UsageStats &Stats, ForStmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_view", S);
  }

  std::string getReferenceViewDecl(ASTContext &C, UsageStats &Stats, ForStmt *S) {
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

    auto &R = C.getSourceManager().getRewriter();
    for (auto *method : Stats.methods) {
      view += TypeToString(method->getReturnType()) + " " + method->getNameAsString() + "(";
      for (auto *arg : method->parameters()) {
        view += TypeToString(arg->getType()) + " " + arg->getNameAsString() + ", ";
      }
      if (method->param_size() != 0) {
        view.pop_back();
        view.pop_back();
      }
      view += ") " + R.getRewrittenText(method->getBody()->getSourceRange()) + "\n";
    }

    view += structName + " &operator[](int i) { return *this; }\n";
    view += "};\n";

    return view;
  }

  std::string getSoaHelperDeclName(UsageStats &Stats, ForStmt *S) {
     return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper", S);
  }

  std::string getSoaHelperDecl(ASTContext &C, UsageStats &Stats, ForStmt *S) {
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

  std::string getSoaBuffersDecl(std::string &sizeExprStr, UsageStats &Stats, ForStmt *S) {
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

  std::string getSoaBuffersInit(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, ForStmt *S) {
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

  std::string getSoaHelperInstanceName(UsageStats &Stats, ForStmt *S) {
      return getUniqueName(Stats.record->getNameAsString() + "_SoaHelper_instance", S);
  }

  std::string getSoaHelperInstance(UsageStats &Stats, ForStmt *S) {
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

  void rewriteLoop(VarDecl *D, Rewriter &R, UsageStats &Stats, ForStmt *S) {
    auto iterVarName = llvm::cast<VarDecl>(llvm::cast<DeclStmt>(S->getInit())->getSingleDecl())->getNameAsString();
    std::string instanceName = getSoaHelperInstanceName(Stats, S);

    if (!llvm::isa<CompoundStmt>(S->getBody())) {
      llvm::errs() << "SOA: loop is not a compound stmt\n";
      exit(1);
    }

    auto bodyBegin = llvm::cast<CompoundStmt>(S->getBody())->getLBracLoc().getLocWithOffset(1);
    std::string source = "\n";
    source += "auto " + D->getNameAsString() + " = " + instanceName + "[" + iterVarName + "];\n";
    R.InsertTextAfter(bodyBegin, source);
  }

  std::string getSoaBuffersWriteback(std::string &sizeExprStr, VarDecl *D, UsageStats &Stats, ForStmt *S) {
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

  SourceLocation getPrologueLoc(ASTContext &C, ForStmt *S) {
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
      auto *parent = GetParent<ForStmt>(C, S);
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getBeginLoc();
      if (parent)
        return parent->getBeginLoc();
      return defaultLoc;
    }
    case decltype(dataMovementKind)::MoveToOutermost: {
      auto *parent = GetParent<ForStmt>(C, S);
      auto *oldParent = parent;
      do {
        oldParent = parent;
        parent = GetParent<ForStmt>(C, oldParent);
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
  SourceLocation getEpilogueLoc(ASTContext &C, ForStmt *S) {
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
      auto *parent = GetParent<ForStmt>(C, S);
      auto *parentAttributed = GetParent<AttributedStmt>(C, parent, true);
      if (parentAttributed)
        return parentAttributed->getEndLoc();
      if (parent)
        return parent->getEndLoc();
      return defaultLoc;
    }
    case decltype(dataMovementKind)::MoveToOutermost: {
      auto *parent = GetParent<ForStmt>(C, S);
      auto *oldParent = parent;
      do {
        oldParent = parent;
        parent = GetParent<ForStmt>(C, oldParent);
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
    auto soaBuffersInit = getSoaBuffersInit(sizeExprStr, targetDecl, usageStats, S);
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
    R.InsertTextBefore(prologueLoc, prologue);

    rewriteLoop(targetDecl, R, usageStats, S);

    auto soaWriteback = getSoaBuffersWriteback(sizeExprStr, targetDecl, usageStats, S);

    std::string epilogue = "\n";
    epilogue += soaWriteback + "\n";
    epilogue += "#pragma clang diagnostic pop\n";

    auto epilogueLoc = getEpilogueLoc(CI.getASTContext(), S).getLocWithOffset(1);
    R.InsertTextAfter(epilogueLoc, epilogue);

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
