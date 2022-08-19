#include "CompressionASTConsumer.h"
#include "CompressionRewriters.h"

void CompressionASTConsumer::HandleTranslationUnit(ASTContext &Context) {
  NewStructForwardDeclAdder(Ctx, SrcMgr, LangOpts, R ).HandleTranslationUnit(Context);
  FriendStructAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  NewStructAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  PragmaPackAdder(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  FieldDeclUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
  GlobalFunctionAndVarUpdater(Ctx, SrcMgr, LangOpts, R).HandleTranslationUnit(Context);
}
