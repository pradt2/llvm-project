//===--- ASTConsumers.cpp - ASTConsumer implementations -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AST Consumer Implementations.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/ASTConsumers.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

//===----------------------------------------------------------------------===//
/// ASTPrinter - Pretty-printer and dumper of ASTs

namespace {
  class RewriterASTConsumer : public ASTConsumer,
                         public RecursiveASTVisitor<RewriterASTConsumer> {
    typedef RecursiveASTVisitor<RewriterASTConsumer> base;

    CompilerInstance &CI;
    Rewriter &R;


  private:
    class SubExprFinder : public ASTConsumer,
                          public RecursiveASTVisitor<SubExprFinder> {
      Expr *child;
      bool found;

    public:
      bool VisitExpr(Expr *E) {
        if (E == child)
          this->found = true;
        return true;
      }

      bool containsSubExpr(Expr *parent, Expr *child) {
        this->found = false;
        this->child = child;
        this->TraverseStmt(parent);
        return this->found;
      }
    };

    static bool containsSubExpr(Expr *parent, Expr *child) {
      bool contains = SubExprFinder().containsSubExpr(parent, child);
      return contains;
    }

    static bool isParticleRecordDecl(RecordDecl *recordDecl) {
      for (auto *field : recordDecl->fields()) {
        for (auto *fieldAttr : field->attrs()) {
          if (fieldAttr->getKind() != clang::attr::Annotate) continue;
          auto *annotateAttr = llvm::cast<AnnotateAttr>(fieldAttr);
          if (annotateAttr->getAnnotation().str() == "compressed ") return true;
        }
      }
      return false;
    }

    template<class ExprClass>
    class SubExprOfTypeFinder : public ASTConsumer,
                          public RecursiveASTVisitor<SubExprFinder> {
      ExprClass *child;

    public:
      bool VisitExpr(Expr *E) {
        if (llvm::isa<ExprClass>(E))
          this->child = llvm::cast_or_null<ExprClass>(E);
        return true;
      }

      ExprClass* containsSubExpr(Expr *parent) {
        this->child = nullptr;
        this->TraverseStmt(parent);
        return this->child;
      }
    };

    class NewStructAdder : public ASTConsumer, public RecursiveASTVisitor<NewStructAdder> {
    private:
      Rewriter &R;
    public:
      explicit NewStructAdder(Rewriter &R) : R(R) {}

      void HandleTranslationUnit(ASTContext &Context) override {
        TranslationUnitDecl *D = Context.getTranslationUnitDecl();
        TraverseDecl(D);
      }

      bool VisitCXXRecordDecl(CXXRecordDecl *decl) {
        if (!RewriterASTConsumer::isParticleRecordDecl(decl)) return true;
        std::string rewrittenSource = "struct Particle__PACKED { char __table[1]; };\n";
        R.InsertTextBefore(decl->getBeginLoc(), rewrittenSource);
        return true;
      }
    };

    template<class ExprClass>
    static bool containsSubExprOfType(Expr *parent) {
      ExprClass *child = SubExprOfTypeFinder<ExprClass>().containsSubExpr(parent);
      return child;
    }

    class VarDeclUpdater : public ASTConsumer, public RecursiveASTVisitor<VarDeclUpdater> {
    private:
      Rewriter &R;
    public:
      explicit VarDeclUpdater(Rewriter &R) : R(R) {}

      void HandleTranslationUnit(ASTContext &Context) override {
        TranslationUnitDecl *D = Context.getTranslationUnitDecl();
        TraverseDecl(D);
      }

      bool VisitVarDecl(VarDecl *decl) {
        std::string ptrs = "";
        auto type = decl->getType();
        while (type->isReferenceType() || type->isAnyPointerType()) {
          if (type->isReferenceType()) {
            type = type.getNonReferenceType();
            ptrs += "&";
          } else if (type->isAnyPointerType()) {
            type = type->getPointeeType();
            ptrs += "*";
          }
        }
        if (!type->isRecordType()) return true;
        auto *record = type->getAsRecordDecl();
        if (!RewriterASTConsumer::isParticleRecordDecl(record)) return true;
        R.ReplaceText(SourceRange(decl->getTypeSpecStartLoc(), decl->getTypeSpecEndLoc()), "Particle__PACKED" + (ptrs.length() > 0 ? " " + ptrs : ""));
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
        if (!RewriterASTConsumer::isParticleRecordDecl(fieldDecl->getParent())) return true;

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

          if (fieldDecl->getNameAsString() == "x") {
            std::string source = "((" + varName + "__table[0] & 1) >> 0)";
            R.ReplaceText(SourceRange(expr->getBeginLoc(), expr->getEndLoc()),
                           source);
            return true;
          }

          if (fieldDecl->getNameAsString() == "y") {
            std::string source = "((" + varName + "__table[0] & 2) >> 1)";
            R.ReplaceText(SourceRange(expr->getBeginLoc(), expr->getEndLoc()),
                           source);
            return true;
          }
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
        if (!RewriterASTConsumer::isParticleRecordDecl(fieldDecl->getParent())) return true;

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
          if (binaryOp->getLHS() != expr || RewriterASTConsumer::containsSubExpr(binaryOp->getRHS(), expr)) {
            llvm::outs() << "Expr is both read and written to?\n";
            return true;
          }

          std::string varName = R.getRewrittenText(SourceRange(expr->getBeginLoc(), expr->getMemberLoc().getLocWithOffset(-1)));

          if (fieldDecl->getNameAsString() == "x") {
            std::string lhsSource = varName + "__table[0]";
            R.ReplaceText(binaryOp->getLHS()->getSourceRange(), lhsSource);
//            auto rhsCurrentExpr = Lexer::getSourceText(CharSourceRange::getTokenRange(binaryOp->getRHS()->getSourceRange()), CI.getSourceManager(), CI.getLangOpts()).str();
            auto rhsCurrentExpr = R.getRewrittenText(binaryOp->getRHS()->getSourceRange());
            std::string rhsSource = "( (" + varName + "__table[0] & ~1) | ( (" + rhsCurrentExpr + ") << 0 ) )";
            R.ReplaceText(binaryOp->getRHS()->getSourceRange(), rhsSource);
            return true;
          }

          if (fieldDecl->getNameAsString() == "y") {
            std::string lhsSource = varName + "__table[0]";
            R.ReplaceText(binaryOp->getLHS()->getSourceRange(), lhsSource);
//            auto rhsCurrentExpr = Lexer::getSourceText(CharSourceRange::getTokenRange(binaryOp->getRHS()->getSourceRange()), CI.getSourceManager(), CI.getLangOpts()).str();
            auto rhsCurrentExpr = R.getRewrittenText(binaryOp->getRHS()->getSourceRange());
            std::string rhsSource = "( (" + varName + "__table[0] & ~2) | ( (" + rhsCurrentExpr + ") << 1 ) )";
            R.ReplaceText(binaryOp->getRHS()->getSourceRange(), rhsSource);
            return true;
          }

        } else if (parentNodeKind.KindId == ASTNodeKind::NKI_CompoundAssignOperator) {
          llvm::outs() << "To be implemented\n";
        }
        return true;
      }
    };

  public:
    RewriterASTConsumer(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      NewStructAdder(R).HandleTranslationUnit(Context);
      VarDeclUpdater(R).HandleTranslationUnit(Context);
      ReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
      WriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
    }

  };

  class ASTPrinter : public ASTConsumer,
                     public RecursiveASTVisitor<ASTPrinter> {
    typedef RecursiveASTVisitor<ASTPrinter> base;

  public:
    enum Kind { DumpFull, Dump, Print, None };
    ASTPrinter(std::unique_ptr<raw_ostream> Out, Kind K,
               ASTDumpOutputFormat Format, StringRef FilterString,
               bool DumpLookups = false, bool DumpDeclTypes = false)
        : Out(Out ? *Out : llvm::outs()), OwnedOut(std::move(Out)),
          OutputKind(K), OutputFormat(Format), FilterString(FilterString),
          DumpLookups(DumpLookups), DumpDeclTypes(DumpDeclTypes) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();

      if (FilterString.empty())
        return print(D);

      TraverseDecl(D);
    }

    bool shouldWalkTypesOfTypeLocs() const { return false; }

    bool TraverseDecl(Decl *D) {
      if (D && filterMatches(D)) {
        bool ShowColors = Out.has_colors();
        if (ShowColors)
          Out.changeColor(raw_ostream::BLUE);

        if (OutputFormat == ADOF_Default)
          Out << (OutputKind != Print ? "Dumping " : "Printing ") << getName(D)
              << ":\n";

        if (ShowColors)
          Out.resetColor();
        print(D);
        Out << "\n";
        // Don't traverse child nodes to avoid output duplication.
        return true;
      }
      return base::TraverseDecl(D);
    }

  private:
    std::string getName(Decl *D) {
      if (isa<NamedDecl>(D))
        return cast<NamedDecl>(D)->getQualifiedNameAsString();
      return "";
    }
    bool filterMatches(Decl *D) {
      return getName(D).find(FilterString) != std::string::npos;
    }
    void print(Decl *D) {
      if (DumpLookups) {
        if (DeclContext *DC = dyn_cast<DeclContext>(D)) {
          if (DC == DC->getPrimaryContext())
            DC->dumpLookups(Out, OutputKind != None, OutputKind == DumpFull);
          else
            Out << "Lookup map is in primary DeclContext "
                << DC->getPrimaryContext() << "\n";
        } else
          Out << "Not a DeclContext\n";
      } else if (OutputKind == Print) {
        PrintingPolicy Policy(D->getASTContext().getLangOpts());
        D->print(Out, Policy, /*Indentation=*/0, /*PrintInstantiation=*/true);
      } else if (OutputKind != None) {
        D->dump(Out, OutputKind == DumpFull, OutputFormat);
      }

      if (DumpDeclTypes) {
        Decl *InnerD = D;
        if (auto *TD = dyn_cast<TemplateDecl>(D))
          InnerD = TD->getTemplatedDecl();

        // FIXME: Support OutputFormat in type dumping.
        // FIXME: Support combining -ast-dump-decl-types with -ast-dump-lookups.
        if (auto *VD = dyn_cast<ValueDecl>(InnerD))
          VD->getType().dump(Out, VD->getASTContext());
        if (auto *TD = dyn_cast<TypeDecl>(InnerD))
          TD->getTypeForDecl()->dump(Out, TD->getASTContext());
      }
    }

    raw_ostream &Out;
    std::unique_ptr<raw_ostream> OwnedOut;

    /// How to output individual declarations.
    Kind OutputKind;

    /// What format should the output take?
    ASTDumpOutputFormat OutputFormat;

    /// Which declarations or DeclContexts to display.
    std::string FilterString;

    /// Whether the primary output is lookup results or declarations. Individual
    /// results will be output with a format determined by OutputKind. This is
    /// incompatible with OutputKind == Print.
    bool DumpLookups;

    /// Whether to dump the type for each declaration dumped.
    bool DumpDeclTypes;
  };

  class ASTDeclNodeLister : public ASTConsumer,
                     public RecursiveASTVisitor<ASTDeclNodeLister> {
  public:
    ASTDeclNodeLister(raw_ostream *Out = nullptr)
        : Out(Out ? *Out : llvm::outs()) {}

    void HandleTranslationUnit(ASTContext &Context) override {
      TraverseDecl(Context.getTranslationUnitDecl());
    }

    bool shouldWalkTypesOfTypeLocs() const { return false; }

    bool VisitNamedDecl(NamedDecl *D) {
      D->printQualifiedName(Out);
      Out << '\n';
      return true;
    }

  private:
    raw_ostream &Out;
  };
} // end anonymous namespace

std::unique_ptr<ASTConsumer>
clang::CreateRewriterASTConsumer(clang::CompilerInstance &CI) {
    return std::make_unique<RewriterASTConsumer>(CI);
}

std::unique_ptr<ASTConsumer>
clang::CreateASTPrinter(std::unique_ptr<raw_ostream> Out,
                        StringRef FilterString) {
  return std::make_unique<ASTPrinter>(std::move(Out), ASTPrinter::Print,
                                       ADOF_Default, FilterString);
}

std::unique_ptr<ASTConsumer>
clang::CreateASTDumper(std::unique_ptr<raw_ostream> Out, StringRef FilterString,
                       bool DumpDecls, bool Deserialize, bool DumpLookups,
                       bool DumpDeclTypes, ASTDumpOutputFormat Format) {
  assert((DumpDecls || Deserialize || DumpLookups) && "nothing to dump");
  return std::make_unique<ASTPrinter>(
      std::move(Out),
      Deserialize ? ASTPrinter::DumpFull
                  : DumpDecls ? ASTPrinter::Dump : ASTPrinter::None,
      Format, FilterString, DumpLookups, DumpDeclTypes);
}

std::unique_ptr<ASTConsumer> clang::CreateASTDeclNodeLister() {
  return std::make_unique<ASTDeclNodeLister>(nullptr);
}

//===----------------------------------------------------------------------===//
/// ASTViewer - AST Visualization

namespace {
class ASTViewer : public ASTConsumer {
  ASTContext *Context = nullptr;

public:
  void Initialize(ASTContext &Context) override { this->Context = &Context; }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I)
      HandleTopLevelSingleDecl(*I);
    return true;
  }

  void HandleTopLevelSingleDecl(Decl *D);
};
}

void ASTViewer::HandleTopLevelSingleDecl(Decl *D) {
  if (isa<FunctionDecl>(D) || isa<ObjCMethodDecl>(D)) {
    D->print(llvm::errs());

    if (Stmt *Body = D->getBody()) {
      llvm::errs() << '\n';
      Body->viewAST();
      llvm::errs() << '\n';
    }
  }
}

std::unique_ptr<ASTConsumer> clang::CreateASTViewer() {
  return std::make_unique<ASTViewer>();
}
