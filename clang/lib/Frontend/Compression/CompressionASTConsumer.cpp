#include "CompressionASTConsumer.h"
#include "CompressionRewriters.h"

void CompressionASTConsumer::HandleTranslationUnit(ASTContext &Context) {
  NewStructForwardDeclAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  FriendStructAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  NewStructAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  PragmaPackAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  FieldDeclUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  LocalVarAndMethodArgUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);

  StaticMethodCallUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  ConstructorExprRewriter(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  FunctionReturnTypeUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  ReadAccessRewriter(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  ConstSizeArrReadAccessRewriter(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  WriteAccessRewriter(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  ConstSizeArrWriteAccessRewriter(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
}
