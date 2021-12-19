//===--- ParseAST.cpp - Provide the clang::ParseAST method ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the clang::ParseAST method.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/ParseAST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Transform/TransformAST.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstdio>
#include <memory>
using namespace clang;

namespace {

using FieldMap = std::map<FieldDecl *, unsigned int>;
using PackedFieldType = std::tuple<clang::CanQualType, unsigned int>;

FieldMap getFieldsMap(RecordDecl *recordDecl) {
  std::map<FieldDecl*, unsigned int> fieldsMap;
  int index = 0;
  for (auto *field : recordDecl->fields()) {
    auto type = field->getType();
    if (!type->isBooleanType()) continue;
    fieldsMap[field] = index;
    index++;
  }
  return fieldsMap;
}

Expr *getGetterExpr(FieldMap fieldMap, FieldDecl *oldField, MemberExpr *packedField, PackedFieldType packedFieldType, QualType targetType) {
  auto &astContext = oldField->getASTContext();

 int index = fieldMap[oldField];
 clang::CanQualType packedType = std::get<clang::CanQualType>(packedFieldType);
 unsigned int packedTypeWidth = std::get<unsigned int>(packedFieldType);
 auto *readPackedFieldExpr = ImplicitCastExpr::Create(astContext, packedType, clang::CastKind::CK_LValueToRValue, packedField, nullptr, clang::ExprValueKind::VK_PRValue, clang::FPOptionsOverride());

 int maskValue = 1 << index;
 auto *maskLiteralExpr = IntegerLiteral::Create(astContext, llvm::APInt(packedTypeWidth, maskValue), packedType, SourceLocation());
 auto *indexLiteralExpr = IntegerLiteral::Create(astContext, llvm::APInt(packedTypeWidth, index), packedType, SourceLocation());
 auto *maskBinOp = clang::BinaryOperator::Create(astContext, readPackedFieldExpr, maskLiteralExpr, clang::BinaryOperator::Opcode::BO_And, packedType, ExprValueKind::VK_PRValue, ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
 auto *bitshiftOp = clang::BinaryOperator::Create(astContext, maskBinOp, indexLiteralExpr, clang::BinaryOperator::Opcode::BO_Shr, packedType, clang::ExprValueKind::VK_PRValue, clang::ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
 auto *outputCast = ImplicitCastExpr::Create(astContext, targetType, clang::CastKind::CK_IntegralToBoolean, bitshiftOp, nullptr, clang::ExprValueKind::VK_PRValue, FPOptionsOverride());
 return outputCast;
}

Expr *getSetterExpr(FieldMap fieldMap, FieldDecl *oldField, MemberExpr *packedField, PackedFieldType packedFieldType, Expr *value) {
  auto &astContext = oldField->getASTContext();

  int index = fieldMap[oldField];
  clang::CanQualType packedType = std::get<clang::CanQualType>(packedFieldType);
  unsigned int packedTypeWidth = std::get<unsigned int>(packedFieldType);
  auto *readPackedFieldExpr = ImplicitCastExpr::Create(astContext, packedType, clang::CastKind::CK_LValueToRValue, packedField, nullptr, clang::ExprValueKind::VK_PRValue, clang::FPOptionsOverride());

  int maskValue = ~(1 << index);
  auto *maskLiteralExpr = IntegerLiteral::Create(astContext, llvm::APInt(packedTypeWidth, maskValue), packedType, SourceLocation());
  auto *indexLiteralExpr = IntegerLiteral::Create(astContext, llvm::APInt(packedTypeWidth, index), packedType, SourceLocation());
  auto *maskBinOp = clang::BinaryOperator::Create(astContext, readPackedFieldExpr, maskLiteralExpr, clang::BinaryOperator::Opcode::BO_And, packedType, ExprValueKind::VK_PRValue, ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
  auto *inputImplicitCast = ImplicitCastExpr::Create(astContext, packedType, clang::CastKind::CK_IntegralCast, value, nullptr, clang::ExprValueKind::VK_PRValue, FPOptionsOverride());
  auto *bitshiftOp = BinaryOperator::Create(astContext, inputImplicitCast, indexLiteralExpr, clang::BinaryOperator::Opcode::BO_Shl, packedType, clang::ExprValueKind::VK_PRValue, clang::ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
  auto *bitwiseOrOp = BinaryOperator::Create(astContext, bitshiftOp, maskBinOp, clang::BinaryOperator::Opcode::BO_Or, packedType, clang::ExprValueKind::VK_PRValue, clang::ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
  auto *assignOp = BinaryOperator::Create(astContext, packedField, bitwiseOrOp, clang::BinaryOperator::Opcode::BO_Assign, packedType, clang::ExprValueKind::VK_PRValue, clang::ExprObjectKind::OK_Ordinary, SourceLocation(), FPOptionsOverride());
  return assignOp;
}

PackedFieldType getPackedType(FieldMap fieldMap, clang::ASTContext &astContext) {
  if (fieldMap.size() <= 8) return PackedFieldType(astContext.UnsignedCharTy, 8);
  if (fieldMap.size() <= 16) return PackedFieldType(astContext.UnsignedShortTy, 16);
  if (fieldMap.size() <= 32) return PackedFieldType(astContext.UnsignedIntTy, 32);
  if (fieldMap.size() <= 64) return PackedFieldType(astContext.UnsignedLongTy, 64);
  if (fieldMap.size() <= 128) return PackedFieldType(astContext.UnsignedInt128Ty, 128);
}


/// Resets LLVM's pretty stack state so that stack traces are printed correctly
/// when there are nested CrashRecoveryContexts and the inner one recovers from
/// a crash.
class ResetStackCleanup
    : public llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup,
                                                   const void> {
public:
  ResetStackCleanup(llvm::CrashRecoveryContext *Context, const void *Top)
      : llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup, const void>(
            Context, Top) {}
  void recoverResources() override {
    llvm::RestorePrettyStackState(resource);
  }
};

/// If a crash happens while the parser is active, an entry is printed for it.
class PrettyStackTraceParserEntry : public llvm::PrettyStackTraceEntry {
  const Parser &P;
public:
  PrettyStackTraceParserEntry(const Parser &p) : P(p) {}
  void print(raw_ostream &OS) const override;
};

/// If a crash happens while the parser is active, print out a line indicating
/// what the current token is.
void PrettyStackTraceParserEntry::print(raw_ostream &OS) const {
  const Token &Tok = P.getCurToken();
  if (Tok.is(tok::eof)) {
    OS << "<eof> parser at end of file\n";
    return;
  }

  if (Tok.getLocation().isInvalid()) {
    OS << "<unknown> parser at unknown location\n";
    return;
  }

  const Preprocessor &PP = P.getPreprocessor();
  Tok.getLocation().print(OS, PP.getSourceManager());
  if (Tok.isAnnotation()) {
    OS << ": at annotation token\n";
  } else {
    // Do the equivalent of PP.getSpelling(Tok) except for the parts that would
    // allocate memory.
    bool Invalid = false;
    const SourceManager &SM = P.getPreprocessor().getSourceManager();
    unsigned Length = Tok.getLength();
    const char *Spelling = SM.getCharacterData(Tok.getLocation(), &Invalid);
    if (Invalid) {
      OS << ": unknown current parser token\n";
      return;
    }
    OS << ": current parser token '" << StringRef(Spelling, Length) << "'\n";
  }
}

}  // namespace

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// ParseAST - Parse the entire file specified, notifying the ASTConsumer as
/// the file is parsed.  This inserts the parsed decls into the translation unit
/// held by Ctx.
///
void clang::ParseAST(Preprocessor &PP, ASTConsumer *Consumer,
                     ASTContext &Ctx, bool PrintStats,
                     TranslationUnitKind TUKind,
                     CodeCompleteConsumer *CompletionConsumer,
                     bool SkipFunctionBodies) {

  std::unique_ptr<Sema> S(
      new Sema(PP, Ctx, *Consumer, TUKind, CompletionConsumer));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Sema> CleanupSema(S.get());

  ParseAST(*S.get(), PrintStats, SkipFunctionBodies);
}

void clang::ParseAST(Sema &S, bool PrintStats, bool SkipFunctionBodies) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  // Also turn on collection of stats inside of the Sema object.
  bool OldCollectStats = PrintStats;
  std::swap(OldCollectStats, S.CollectStats);

  // Initialize the template instantiation observer chain.
  // FIXME: See note on "finalize" below.
  initialize(S.TemplateInstCallbacks, S);

  ASTConsumer *Consumer = &S.getASTConsumer();

  std::unique_ptr<Parser> ParseOP(
      new Parser(S.getPreprocessor(), S, SkipFunctionBodies));
  Parser &P = *ParseOP.get();

  llvm::CrashRecoveryContextCleanupRegistrar<const void, ResetStackCleanup>
      CleanupPrettyStack(llvm::SavePrettyStackState());
  PrettyStackTraceParserEntry CrashInfo(P);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Parser>
    CleanupParser(ParseOP.get());

  S.getPreprocessor().EnterMainSourceFile();
  ExternalASTSource *External = S.getASTContext().getExternalSource();
  if (External)
    External->StartTranslationUnit(Consumer);

  // If a PCH through header is specified that does not have an include in
  // the source, or a PCH is being created with #pragma hdrstop with nothing
  // after the pragma, there won't be any tokens or a Lexer.
  bool HaveLexer = S.getPreprocessor().getCurrentLexer();

  llvm::SmallVector<Parser::DeclGroupPtrTy> decls;

  if (HaveLexer) {
    llvm::TimeTraceScope TimeScope("Frontend");
    P.Initialize();
    Parser::DeclGroupPtrTy ADecl;
    Sema::ModuleImportState ImportState;
    EnterExpressionEvaluationContext PotentiallyEvaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

    for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl, ImportState); !AtEOF;
         AtEOF = P.ParseTopLevelDecl(ADecl, ImportState)) {
      decls.push_back(ADecl);
  }

  FieldDecl *alternativeField;
  PackedFieldType packedFieldType;
  CXXRecordDecl *alternativeRecordDecl;
  CXXConstructorDecl *alternativeRecordConstructorDecl;
  CXXRecordDecl *rewrittenRecordDeclOld;
  FieldMap fieldMap;
  clang::CanQualType packedType;
  auto &astContext = S.getASTContext();

//  for (auto dDecl : decls) {
//    for (auto *decl : dDecl.get()) {
//      if (decl->getKind() != Decl::CXXRecord) continue;
//      auto *recordDecl = llvm::cast<CXXRecordDecl>(decl);
//      if (!recordDecl->isCompleteDefinition()) continue;
//      if (recordDecl->getNameAsString().find("PackedData") != 0) continue;
//      rewrittenRecordDeclOld = recordDecl;
//      fieldMap = getFieldsMap(rewrittenRecordDeclOld);
//      packedFieldType = getPackedType(fieldMap, astContext);
//      packedType = std::get<clang::CanQualType>(packedFieldType);
//      for (int i = 0; i < 1; i++) {
//        auto &classIdentifierInfo = astContext.Idents.getOwn(llvm::StringRef(recordDecl->getDeclName().getAsString() + "$$Packed"));
//        alternativeRecordDecl = CXXRecordDecl::Create(astContext, TTK_Struct, astContext.getTranslationUnitDecl(), SourceLocation(), SourceLocation(), &classIdentifierInfo);
//        astContext.getTranslationUnitDecl()->addDecl(alternativeRecordDecl);
//        auto &fieldIdentifierInfo = astContext.Idents.getOwn("__packed_bytes");
//        alternativeField = FieldDecl::Create(astContext, alternativeRecordDecl, SourceLocation(), SourceLocation(), &fieldIdentifierInfo, packedType, astContext.getTrivialTypeSourceInfo(packedType), nullptr, false, ICIS_NoInit);
//        alternativeField->setAccess(AS_public);
//        alternativeRecordDecl->startDefinition();
//        alternativeRecordDecl->addDecl(alternativeField);
//        alternativeRecordDecl->setReferenced();
//        alternativeRecordConstructorDecl = S.DeclareImplicitDefaultConstructor(alternativeRecordDecl); // this already calls CXXRecordDecl->addDecl
//        alternativeRecordDecl->completeDefinition();
//      }
//    }
//
//    for (auto *decl : dDecl.get()) {
//      if (decl->getKind() != Decl::Function) continue;
//      auto *functionDecl = llvm::cast<FunctionDecl>(decl);
//      if (!functionDecl->isMain()) continue;
//
//      // changing type of 'packedDataObj' variable decl
//      for (auto *stmt : llvm::cast<CompoundStmt>(functionDecl->getBody())->body()) {
//        if (!DeclStmt::classof(stmt)) continue;
//        auto *declStmt = llvm::cast<DeclStmt>(stmt);
//        if (!declStmt->isSingleDecl()) continue;
//        auto *declStmtDecl = declStmt->getSingleDecl();
//        if (!VarDecl::classof(declStmtDecl)) continue;
//        auto *varDecl = llvm::cast<VarDecl>(declStmtDecl);
//        if (varDecl->getNameAsString().find("packedDataObj") == std::string::npos) continue;
//        QualType varType = varDecl->getType();
//        if (!varType->isRecordType()) continue;
//        auto *oldRecordDecl = varType->getAsCXXRecordDecl();
//        if (oldRecordDecl != rewrittenRecordDeclOld) continue;
//        varDecl->setDeclName(alternativeRecordDecl->getDeclName());
//        varDecl->setType(astContext.getRecordType(alternativeRecordDecl));
//        varDecl->setInit(CXXConstructExpr::Create(astContext, astContext.getRecordType(alternativeRecordDecl), SourceLocation(), alternativeRecordConstructorDecl, false, llvm::ArrayRef<Expr*>(), false, false, false, false, CXXConstructExpr::CK_Complete, SourceRange()));
//      }
//
//      // change of the return statement
//      for (auto *stmt : llvm::cast<CompoundStmt>(functionDecl->getBody())->body()) {
//        if (!ReturnStmt::classof(stmt)) {
//          continue;
//        }
//        auto *returnStmt = llvm::cast<ReturnStmt>(stmt);
//        auto *returnVal = returnStmt->getRetValue();
//        if (!ImplicitCastExpr::classof(returnVal)) {
//          continue;
//        }
//        auto *outerImplicitCastExpr = llvm::cast<ImplicitCastExpr>(returnVal);
//        auto *insideImplicitCastExpr = outerImplicitCastExpr->getSubExpr();
//        if (!ImplicitCastExpr::classof(insideImplicitCastExpr)) {
//          continue;
//        }
//        insideImplicitCastExpr = llvm::cast<ImplicitCastExpr>(insideImplicitCastExpr)->getSubExpr();
//        if (!MemberExpr::classof(insideImplicitCastExpr)) {
//          continue;
//        }
//        auto *memberExpr = llvm::cast<MemberExpr>(insideImplicitCastExpr);
//        auto *memberExprBaseExpr = memberExpr->getBase();
//        if (!DeclRefExpr::classof(memberExprBaseExpr)) {
//          continue;
//        }
//
//        auto *declRefExpr = llvm::cast<DeclRefExpr>(memberExprBaseExpr);
//        auto *refdTypeDecl = declRefExpr->getDecl()->getType()->getAsCXXRecordDecl();
//        if (refdTypeDecl != alternativeRecordDecl) {
//          continue;
//        }
//
//        auto *oldField = llvm::cast<FieldDecl>(memberExpr->getMemberDecl());
//        auto *newDeclRefExpr = DeclRefExpr::Create(astContext, clang::NestedNameSpecifierLoc(), SourceLocation(), declRefExpr->getDecl(), false, SourceLocation(), declRefExpr->getDecl()->getType(), clang::ExprValueKind::VK_LValue);
//        auto *newMemberExpr = MemberExpr::CreateImplicit(astContext, newDeclRefExpr, false, alternativeField, packedType, clang::ExprValueKind::VK_LValue, clang::ExprObjectKind::OK_Ordinary);
//        outerImplicitCastExpr->setSubExpr(getGetterExpr(fieldMap, oldField, newMemberExpr, packedFieldType, memberExpr->getType()));
//      }
//
//      // change the value assignment operation on the 'packedDataObj.packedA = true' line
//      for (auto *stmt : llvm::cast<CompoundStmt>(functionDecl->getBody())->body()) {
//        if (!BinaryOperator::classof(stmt)) {
//          continue;
//        }
//        auto *binopStmt = llvm::cast<BinaryOperator>(stmt);
//        if (!binopStmt->isAssignmentOp()) {
//          continue;
//        }
//        auto *binopLhs = binopStmt->getLHS();
//        if (!MemberExpr::classof(binopLhs)) {
//          continue;
//        }
//        auto memberExpr = llvm::cast<MemberExpr>(binopLhs);
//        auto *memberExprBaseExpr = memberExpr->getBase();
//        if (!DeclRefExpr::classof(memberExprBaseExpr)) {
//          continue;
//        }
//        auto *declRefExpr = llvm::cast<DeclRefExpr>(memberExprBaseExpr);
//        auto *refdTypeDecl = declRefExpr->getDecl()->getType()->getAsCXXRecordDecl();
//        if (refdTypeDecl != alternativeRecordDecl) {
//          continue;
//        }
//
//        auto *oldField = llvm::cast<FieldDecl>(memberExpr->getMemberDecl());
//        auto *newDeclRefExpr = DeclRefExpr::Create(astContext, clang::NestedNameSpecifierLoc(), SourceLocation(), declRefExpr->getDecl(), false, SourceLocation(), declRefExpr->getDecl()->getType(), clang::ExprValueKind::VK_LValue);
//        auto *newMemberExpr = MemberExpr::CreateImplicit(astContext, newDeclRefExpr, false, alternativeField, packedType, clang::ExprValueKind::VK_LValue, clang::ExprObjectKind::OK_Ordinary);
//        auto *assignStmt = (clang::BinaryOperator *) getSetterExpr(fieldMap, oldField, newMemberExpr, packedFieldType, binopStmt->getRHS());
//        binopStmt->setLHS(assignStmt->getLHS());
//        binopStmt->setRHS(assignStmt->getRHS());
//        binopStmt->setOpcode(assignStmt->getOpcode());
//        binopStmt->setType(assignStmt->getType());
//        binopStmt->setValueKind(assignStmt->getValueKind());
//        binopStmt->setObjectKind(assignStmt->getObjectKind());
//        bool hasStoredFeatures = assignStmt->hasStoredFPFeatures();
//        if (hasStoredFeatures) {
//          binopStmt->setStoredFPFeatures(assignStmt->getStoredFPFeatures());
//          binopStmt->setHasStoredFPFeatures(assignStmt->hasStoredFPFeatures());
//        }
//      }
//
//      // change of the return statement if we return sizeof(packedDataObj)
//      for (auto *stmt : llvm::cast<CompoundStmt>(functionDecl->getBody())->body()) {
//        if (!ReturnStmt::classof(stmt)) {
//          continue;
//        }
//        auto *returnVal = llvm::cast<ReturnStmt>(stmt)->getRetValue();
//        if (!ImplicitCastExpr::classof(returnVal)) {
//          continue;
//        }
//        auto *implicitCastExpr = llvm::cast<ImplicitCastExpr>(returnVal)->getSubExpr();
//        if (!UnaryExprOrTypeTraitExpr::classof(implicitCastExpr)) {
//          continue;
//        }
//        auto *declRefExprMaybe = llvm::cast<UnaryExprOrTypeTraitExpr>(implicitCastExpr)->getArgumentExpr();
//        if (!DeclRefExpr::classof(declRefExprMaybe)) {
//          continue;
//        }
//        auto *declRefExpr = llvm::cast<DeclRefExpr>(declRefExprMaybe);
//        auto *valueDecl = declRefExpr->getDecl();
//        if (llvm::cast<VarDecl>(valueDecl)->getType()->getAsCXXRecordDecl() != alternativeRecordDecl) continue;
//        auto *replacementDeclRefExpr = DeclRefExpr::Create(astContext, NestedNameSpecifierLoc(), SourceLocation(), valueDecl, false, DeclarationNameInfo(), valueDecl->getType(), ExprValueKind::VK_LValue);
//        llvm::cast<UnaryExprOrTypeTraitExpr>(implicitCastExpr)->setArgument(replacementDeclRefExpr);
//      }
//    }
//  }

  auto oldType = astContext.LongTy;
  auto *newRecord = createNewEmptyRecord(astContext, S, "NewRecord");
  auto newType = astContext.getRecordType(newRecord);
  auto *newTypeInitExpr = getInitExpr(astContext, newRecord);
  auto *visitor = new ASTTypeSwitcher(astContext, oldType, newType, newTypeInitExpr);
  visitor->TraverseDecl(astContext.getTranslationUnitDecl());

  for (auto aDecl : decls) {
    // If we got a null return and something *was* parsed, ignore it.  This
    // is due to a top-level semicolon, an action override, or a parse error
    // skipping something.
    if (aDecl && !Consumer->HandleTopLevelDecl(aDecl.get()))
    return;
    }
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls())
    Consumer->HandleTopLevelDecl(DeclGroupRef(D));

  Consumer->HandleTranslationUnit(S.getASTContext());

  // Finalize the template instantiation observer chain.
  // FIXME: This (and init.) should be done in the Sema class, but because
  // Sema does not have a reliable "Finalize" function (it has a
  // destructor, but it is not guaranteed to be called ("-disable-free")).
  // So, do the initialization above and do the finalization here:
  finalize(S.TemplateInstCallbacks, S);

  std::swap(OldCollectStats, S.CollectStats);
  if (PrintStats) {
    llvm::errs() << "\nSTATISTICS:\n";
    if (HaveLexer) P.getActions().PrintStats();
    S.getASTContext().PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
  }
}

