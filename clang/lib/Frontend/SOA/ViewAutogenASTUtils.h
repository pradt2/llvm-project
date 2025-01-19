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
      auto numArgs = E->getNumArgs();
      for (int i = 0; i < numArgs; i++) {
        auto *argExpr = IgnoreImplicitCasts(E->getArgs()[i]);
        if (llvm::isa<ArraySubscriptExpr>(argExpr)) {
          auto *arrOp = llvm::cast<ArraySubscriptExpr>(argExpr);
          argExpr = arrOp->getBase();
        }
        if (!llvm::isa<DeclRefExpr>(argExpr)) continue;
        auto *declRefExpr = llvm::cast<DeclRefExpr>(argExpr);
        if (D != declRefExpr->getDecl()) continue;

        auto *callee = E->getDirectCallee();
        (*fs)[callee] = i;
        return true;
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

void PrintUsages2(VarDecl *D, Stmt *S) {
  CXXRecordDecl *decl;
  if (D->getType()->isPointerType()) {
    decl = D->getType()->getPointeeType()->getAsCXXRecordDecl();
  } else if (D->getType()->isReferenceType()) {
    decl = D->getType().getNonReferenceType()->getAsCXXRecordDecl();
  } else decl = D->getType()->getAsCXXRecordDecl();

  UsageStats stats {
      .record = decl,
      .fields = {},
      .methods = {}
  };

  FindUsages(D, stats, S);

  auto view = CreateReferenceView(D->getASTContext(), &stats);
  printf("%s", view.c_str());
  return;
}
