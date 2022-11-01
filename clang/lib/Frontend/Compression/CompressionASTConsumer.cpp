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

void ForceFloatLiteralASTConsumer::HandleTranslationUnit(clang::ASTContext &Context) {
  TranslationUnitDecl *D = Context.getTranslationUnitDecl();
  TraverseDecl(D);
}

bool ForceFloatLiteralASTConsumer::VisitFloatingLiteral(clang::FloatingLiteral *FL) {
  if (FL->getType()->getAs<BuiltinType>()->getKind() != clang::BuiltinType::Double) return true;
  auto location = FL->getLocation(); // for some reason both getBeginLod and getEndLoc point to the start loc
  R.InsertTextBefore(location, llvm::StringRef("(double)"));
  return true;
}

void ForceFloatASTConsumer::HandleTranslationUnit(clang::ASTContext &Context) {
  TranslationUnitDecl *D = Context.getTranslationUnitDecl();
  TraverseDecl(D);
  forceFloat();
}

bool ForceFloatASTConsumer::VisitDecl(Decl *D) {
  if (D->isInStdNamespace()) return true;
  auto fileId = SrcMgr.getFileID(D->getLocation());
  auto *entry = SrcMgr.getFileEntryForID(fileId);

  if (!entry || !entry->isValid()) return true;
  auto path = entry->getName(); //SrcMgr.getFilename also possible
  if (path.startswith(llvm::StringRef("/usr"))) return true;
  if (path.contains(llvm::StringRef("/lib/gcc/x86_64-pc-linux-gnu"))) return true;
  fileMap[path.str()] = "";
  return true;
}

void myReplace(std::string &str, const char* from, const char *to) {
  std::string from_str = std::string(from);
  std::string to_str = std::string(to);

  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from_str.length(), to_str);
    start_pos += to_str.length(); // Handles case where 'to' is a substring of 'from'
  }
}

void ForceFloatASTConsumer::forceFloat() {
  for (const auto &kv : fileMap) {
    auto filePath = kv.first;

    std::string fileContent;
    for (const auto &remappedKv : PP.RemappedFileBuffers) {
      auto remappedPath = remappedKv.first;
      auto *remappedContent = remappedKv.second;
      if (filePath == remappedPath) {
        fileContent = remappedContent->getBuffer().str();
        goto L1;
      }
    }

    fileContent = SrcMgr.getFileManager().getBufferForFile(filePath).get().get()->getBuffer().str();

  L1:
    myReplace(fileContent, "double /*!keep*/", "double_keep");
    myReplace(fileContent, "long double", "XWXWX-long-double-XWXWX");
    myReplace(fileContent, "double ", "float "); // normal type, e.g. function return type
    myReplace(fileContent, "double*", "float*"); // pointer type
    myReplace(fileContent, "double&", "float&"); // reference type
    myReplace(fileContent, "double,", "float,"); // type reference
    myReplace(fileContent, "double{", "float{"); // ??
    myReplace(fileContent, "double[", "float["); // array type
    myReplace(fileContent, "double>", "float>"); // template specialisation type
    myReplace(fileContent, "double)", "float)"); // type cast
    myReplace(fileContent, "double(", "float("); // std::function< double(...    <- lambda ret type definition
    myReplace(fileContent, "XWXWX-long-double-XWXWX", "long double");
    myReplace(fileContent, "double_keep", "double /*!keep*/");

    fileMap[filePath] = fileContent;
  }

}