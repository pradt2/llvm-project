#pragma once

#include <optional>

using namespace clang;

struct DataMember {
  enum DataFlow {
    Unknown = 0,
    Read = 1,
    Write = 2,
    Members = 4,
  };

  enum Syntax {
    None = 0,
    Call = 1,
    Subscript = 2,
  };

  std::string name;
  QualType type;
  DataFlow dataFlow = Unknown;
  Syntax syntax = None;
  std::vector<DataMember> members;
  DataMember *parent = nullptr;
};

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

Expr *GetImmediateParent(ASTContext &C, Expr *E) {
  for (auto parent: C.getParentMapContext().getParents(*E)) {
    auto *exprMaybe = parent.get<Expr>();
    if (exprMaybe) return (Expr*) exprMaybe;
  }
  return nullptr;
}

static void PrintUsages(VarDecl *V, Stmt *S) {
  auto isPointer = V->getType()->isPointerType();
  auto isRef = V->getType()->isReferenceType();

  DataMember rootMember {
      .name = V->getNameAsString(),
      .type = V->getType(),
  };

  struct MemberFinder : RecursiveASTVisitor<MemberFinder> {
    VarDecl *D;
    DataMember *rootMember;

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      if (E->getDecl() != D) return true;
      auto &C = D->getASTContext();

      auto *parent = GetImmediateParent(C, E);
      Expr *lastMember = nullptr;
      DataMember *lastDataMember = this->rootMember;
      printf("--- new member ---\n");

      while (parent) {
        if (llvm::isa<ImplicitCastExpr>(parent)) {
          // do nothing, search parents
        } else if (llvm::isa<CXXDependentScopeMemberExpr>(parent)) {
          // we're inside a template, don't panic
          auto *memberExpr = llvm::cast<CXXDependentScopeMemberExpr>(parent);
          auto name = memberExpr->getMemberNameInfo().getAsString();
          printf("Found (template) member: %s\n", name.c_str());
          lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Members);
          DataMember *existingMemberMaybe = nullptr;
          for (auto &existingMember : lastDataMember->members) if (existingMember.name == name) existingMemberMaybe = &existingMember;
          if (!existingMemberMaybe) {
            DataMember d{
              .name = name.c_str(),
              .type = memberExpr->getType(),
              .parent = lastDataMember,
            };
            lastDataMember->members.push_back(d);
            lastDataMember = &lastDataMember->members.back();
          } else lastDataMember = existingMemberMaybe;
          lastMember = memberExpr;
        } else if (llvm::isa<MemberExpr>(parent)) {
          auto *memberExpr = llvm::cast<CXXDependentScopeMemberExpr>(parent);
          auto name = memberExpr->getMemberNameInfo().getAsString();
          printf("Found member: %s\n", name.c_str());
          lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Members);
          DataMember *existingMemberMaybe = nullptr;
          for (auto &existingMember : lastDataMember->members) if (existingMember.name == name) existingMemberMaybe = &existingMember;
          if (!existingMemberMaybe) {
            DataMember d{
                .name = name.c_str(),
                .type = memberExpr->getType(),
                .parent = lastDataMember,
            };
            lastDataMember->members.push_back(d);
            lastDataMember = &lastDataMember->members.back();
          } else lastDataMember = existingMemberMaybe;
          lastMember = memberExpr;
        } else if (llvm::isa<CallExpr>(parent)) {
          auto *callExpr = llvm::cast<CallExpr>(parent);
          auto *callee = callExpr->getCallee();
          if (lastMember != callee) {
            printf("Member used to read\n");
            lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
            break;
          }
          printf("Member is a function\n");
          lastDataMember->syntax = (DataMember::Syntax) (lastDataMember->syntax | DataMember::Syntax::Call);
          lastMember = callExpr;
        } else if (llvm::isa<BinaryOperator>(parent)) {
          auto *binOp = llvm::cast<BinaryOperator>(parent);
          if (!binOp->isAssignmentOp()) {
            printf("Member used to read\n");
            lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
            break;
          }
          if (binOp->getRHS() == lastMember) {
            printf("Member used to read\n");
            lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
            break;
          }
          if (binOp->isCompoundAssignmentOp()) {
            printf("Member used to read\n");
            lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
          }
          printf("Member used to write\n");
          lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Write);
          break;
        } else if (llvm::isa<ArraySubscriptExpr>(parent)) {
          auto *arraySubscriptOp = llvm::cast<ArraySubscriptExpr>(parent);
          if (parent == arraySubscriptOp->getIdx()) {
            printf("Member used to read\n");
            lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
            break;
          }
          printf("Member uses subscript ops\n");
          lastDataMember->syntax = (DataMember::Syntax) (lastDataMember->syntax | DataMember::Syntax::Subscript);
          lastMember = arraySubscriptOp;
        } else {
          printf("Member used to read (unknown node)\n");
          lastDataMember->dataFlow = (DataMember::DataFlow) (lastDataMember->dataFlow | DataMember::DataFlow::Read);
          break;
        }

        parent = GetImmediateParent(C, parent);
      }
      return true;
    }
  } M{.D = V, .rootMember = &rootMember};

  M.TraverseStmt(S);

  return;
}

struct UsageStats {
  enum UsageKind {
    Unknown = 0, Read = 1,
    Write = 2,
  };

  CXXRecordDecl *record;
  std::map<FieldDecl*, UsageKind> fields;
  std::set<CXXMethodDecl*> methods;
};

template<typename T, typename U>
T *GetImmediateParent(ASTContext &C, U *E) {
  for (auto parent: C.getParentMapContext().getParents(*E)) {
    auto *exprMaybe = parent.template get<T>();
    if (exprMaybe) return (T*) exprMaybe;
  }
  return nullptr;
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

std::string CreateReferenceView(ASTContext &C, UsageStats *S) {
  std::string view = "";
  view =+ "struct " + S->record->getNameAsString() + "_view { \n";
  for (auto [k, v] : S->fields) {
    if (!k->getType()->isArrayType()) {
      view += k->getType().getAsString() + (k->getType()->isReferenceType() ? " " : " &") + k->getNameAsString() + ";\n";
    } else {
      auto *arrType = llvm::cast<ConstantArrayType>(k->getType()->getAsArrayTypeUnsafe());
      auto size = arrType->getSExtSize();
      view += arrType->getElementType().getAsString() + " " + k->getNameAsString() + "[" + std::to_string(size) + "];\n";
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
    if (!llvm::isa<DeclRefExpr>(base)) return true;
    auto *declRefBase = llvm::cast<DeclRefExpr>(base);
    if (declRefBase->getDecl() != D) return true;

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

std::map<FunctionDecl *, int> FindLeakFunctions(VarDecl *D, Stmt *S) {
  struct LeakFinder : RecursiveASTVisitor<LeakFinder> {
    VarDecl *D;
    std::map<FunctionDecl *, int> *fs;

    bool VisitCallExpr(CallExpr *E) {
      auto numArgs = E->getNumArgs();
      for (int i = 0; i < numArgs; i++) {
        auto *argExpr = E->getArgs()[i];
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

  std::map<FunctionDecl *, int> functions;
  LeakFinder{.D = D, .fs = &functions}.TraverseStmt(S);
  return functions;
}

void FindUsages(VarDecl *D, UsageStats &stats, Stmt *S) {
  UsageFinder f{.D = D, .Stats = &stats};
  f.TraverseStmt(S);

  while (true) {
    auto oldMethodCount = stats.methods.size();
    for (auto *method : stats.methods) {
      UsageFinder{.D = D, .Stats = &stats}.TraverseCXXMethodDecl(method);
    }
    auto hasMethodCountChanged = stats.methods.size() > oldMethodCount;
    if (!hasMethodCountChanged) break;
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
