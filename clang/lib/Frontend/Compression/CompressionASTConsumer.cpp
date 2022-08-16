#include "CompressionASTConsumer.h"
#include "CompressionRewriters.h"

void CompressionASTConsumer::HandleTranslationUnit(ASTContext &Context) override {
  NewStructForwardDeclAdder(R, CI).HandleTranslationUnit(Context);
  FriendStructAdder(R, CI).HandleTranslationUnit(Context);
  NewStructAdder(R, CI).HandleTranslationUnit(Context);
  PragmaPackAdder(R, CI).HandleTranslationUnit(Context);
  FieldDeclUpdater(R, CI).HandleTranslationUnit(Context);
  LocalVarAndMethodArgUpdater(R, CI).HandleTranslationUnit(Context);

  StaticMethodCallUpdater(R, CI).HandleTranslationUnit(Context);
  ConstructorExprRewriter(R, CI).HandleTranslationUnit(Context);
  FunctionReturnTypeUpdater(R, CI).HandleTranslationUnit(Context);
  ReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
  ConstSizeArrReadAccessRewriter(R, CI).HandleTranslationUnit(Context);
  WriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
  ConstSizeArrWriteAccessRewriter(R, CI).HandleTranslationUnit(Context);
}
