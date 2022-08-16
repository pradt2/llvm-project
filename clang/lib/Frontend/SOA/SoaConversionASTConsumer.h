#ifndef CLANG_SOACONVERSIONASTCONSUMER_H
#define CLANG_SOACONVERSIONASTCONSUMER_H

#include "../SemaIR/SemaIR.h"

using namespace clang;

class SoaConversionASTConsumer : public ASTConsumer, public RecursiveASTVisitor<SoaConversionASTConsumer> {

  CompilerInstance &CI;
  Rewriter &R;

  template<typename Attr>
  const Attr *getAttr(Stmt *S) {
      auto const *stmt = dyn_cast_or_null<AttributedStmt>(S);
      if (!stmt) {
        for (auto parent: CI.getASTContext().getParentMapContext().getParents(*S)) {
          if (parent.getNodeKind().KindId != ASTNodeKind::NodeKindId::NKI_AttributedStmt) {
            continue;
          }
          auto *genericStmt = parent.get<Stmt>();
          if (!llvm::isa<AttributedStmt>(genericStmt)) continue;
          stmt = llvm::cast<AttributedStmt>(genericStmt);
          break;
        }
      }
      if (!stmt) return nullptr;
      for (auto *attr : stmt->getAttrs()) {
        if (!llvm::isa<Attr>(attr)) continue;
        return llvm::cast<Attr>(attr);
      }
      return nullptr;
  }

  FunctionDecl *getLoopParentFunctionDecl(Stmt *S) {
    DynTypedNode node = DynTypedNode::create(*S);
    while (true) {
      auto parents = CI.getASTContext().getParentMapContext().getParents(node);
      if (parents.empty()) return nullptr;
      if (parents.size() != 1) {
        llvm::errs() << "Too many parents " << __FILE__ << ":" << __LINE__ << "\n";
        return nullptr;
      }
      auto parent = parents[0];
      if (parent.getNodeKind().KindId == ASTNodeKind::NodeKindId::NKI_TranslationUnitDecl) return nullptr;
      if (parent.getNodeKind().KindId == ASTNodeKind::NodeKindId::NKI_FunctionDecl) return const_cast<FunctionDecl *>(parent.get<FunctionDecl>());
      node = parent;
    }
  }

  DeclRefExpr *getTargetDeclRefExpr(llvm::StringRef targetRef, FunctionDecl *loopParent) {
    class DeclRefExprFinder : public ASTConsumer, public RecursiveASTVisitor<DeclRefExprFinder> {
      std::string name;
      DeclRefExpr *declExpr;
    public:
      DeclRefExpr *findDeclRefExpr(std::string name, Decl *context) {
        this->declExpr = nullptr;
        this->name = name;
        this->TraverseDecl(context);
        return declExpr;
      }

      bool VisitDeclRefExpr(DeclRefExpr *expr) {
        if (name != expr->getNameInfo().getName().getAsString()) return true;
        declExpr = expr;
        return false;
      }
    } finder;
    auto *declRef = finder.findDeclRefExpr(targetRef.str(), loopParent);
    return declRef;
  }

  QualType stripPointersRefsVectorsConstSizeArrType (QualType type) {
    bool canonLoop = false;
    while (type->isReferenceType() || type->isPointerType() || type->isConstantArrayType() || !type.isCanonical()) {
      if (type->isReferenceType()) {
        type = type.getNonReferenceType();
        canonLoop = false;
        continue;
      }
      if (type->isPointerType()) {
        type = type->getPointeeType();
        canonLoop = false;
        continue;
      }
      if (type->isConstantArrayType()) {
        type = llvm::cast<ConstantArrayType>(type->castAsArrayTypeUnsafe())->getElementType();
        canonLoop = false;
        continue;
      }
      if (!type.isCanonical()) {
        auto canonType = type.getCanonicalType();
        if (type == canonType) {
          if (canonLoop) break;
          canonLoop = true;
        }
        type = canonType;
      }
    }
    return type;
  }

  QualType getMostLikelyIterableType(QualType type) {
    type = stripPointersRefsVectorsConstSizeArrType(type);
    auto *recordDecl = type->getAsCXXRecordDecl();
    if (!recordDecl) return type;
    CXXMethodDecl *operatorMethod = nullptr;
    for (auto *method : recordDecl->methods()) {
      if (method->getNameAsString() != "operator[]") continue;
      if (method->isConst()) continue;
      operatorMethod = method;
      break;
    }
    if (!operatorMethod) return type;
    type = this->stripPointersRefsVectorsConstSizeArrType(operatorMethod->getReturnType());
    return type;
  }

  QualType getIndirectType (QualType type) {
    while (type->isReferenceType() || type->isPointerType()) {
      if (type->isReferenceType()) type = type.getNonReferenceType();
      if (type->isPointerType()) type = type->getPointeeType();
    }
    return type;
  }

  std::string getAccessToType(QualType type, std::string varName) {
    std::string out = varName;
    while (type->isReferenceType() || type->isPointerType()) {
      if (type->isReferenceType()) {
        out = "(" + out + ")";
        type = type.getNonReferenceType();
      }
      else if (type->isPointerType()) {
        out = "(*" + out + ")";
        type = type->getPointeeType();
      }
    }
    return out;
  }

  RecordDecl *getTargetRecordDecl(DeclRefExpr *declRef, FunctionDecl *loopParent) {
    auto type = declRef->getDecl()->getType();
    type = getMostLikelyIterableType(type);
    if (!type->isRecordType()) return nullptr;
    auto *record = type->getAsRecordDecl();
    return record;
  }

  std::vector<std::string> splitString(llvm::StringRef fields, StringRef separator) {
    std::vector<std::string> fieldsVector;
    do {
      auto tuple = fields.split(separator);
      fieldsVector.push_back(tuple.first.trim().str());
      fields = tuple.second;
    } while (fields != "");
    return fieldsVector;
  }

  std::vector<FieldDecl *> resolveFieldDecl(RecordDecl *decl, llvm::StringRef fieldPath) {
    std::vector<std::string> fieldPathNames = splitString(fieldPath, ".");
    std::vector<FieldDecl *> fieldsDecls;
    bool foundDecl;
    for (unsigned i = 0; i < fieldPathNames.size(); i++) {
      auto fieldName = fieldPathNames[i];
      foundDecl = false;
      for (auto *field : decl->fields()) {
        if (field->getName() != fieldName)
          continue;
        fieldsDecls.push_back(field);
        QualType fieldType = getIndirectType(field->getType());
        if (fieldType->isRecordType()) {
          decl = fieldType->getAsRecordDecl();
          foundDecl = true;
          break;
        }
        if (i == fieldPathNames.size() - 1) {
          foundDecl = true;
          break;
        }
        llvm::errs() << "Nested non-leaf field type is not Record " << __FILE__
                     << ":" << __LINE__ << "\n";
        return std::vector<FieldDecl *>();
      }
      if (!foundDecl) {
        llvm::errs() << "Could not find field " << __FILE__ << ":" << __LINE__
                     << "\n";
        return std::vector<FieldDecl *>();
      }
    }
    return fieldsDecls;
  }

  FieldDecl *getNestedFieldDecl(RecordDecl *decl, llvm::StringRef fieldPath) {
    auto *fieldDecl = resolveFieldDecl(decl, fieldPath).back();
    return fieldDecl;
  }

  std::vector<FieldDecl *> getFieldsForSoa(RecordDecl *decl, llvm::StringRef *fields, unsigned int fieldsCount) {
    std::vector<FieldDecl *> foundFields;
    for (unsigned int i = 0; i < fieldsCount; i++) {
      auto *newField = getNestedFieldDecl(decl, fields[i]);
      foundFields.push_back(newField);
    }
    return foundFields;
  }

  std::string getSoaDef(llvm::StringRef targetRef, std::vector<FieldDecl *> fromFieldDecls, llvm::StringRef size) {
    std::string instanceName = targetRef.str() + "__SoA__instance";
    std::string sourceCode = "struct " + targetRef.str() + "__SoA {\n";
    for (auto *field : fromFieldDecls) {
      auto type = toSource(*fromQualType(field->getType(), field->getASTContext()));
      auto name = field->getNameAsString();
      sourceCode += "    " + type + " * " + "(" + name + ");\n";
    }
    sourceCode += "\n} " + instanceName + ";\n\n";

    for (auto *field : fromFieldDecls) {
      auto type = toSource(*fromQualType(field->getType(), field->getASTContext()));
      auto name = field->getNameAsString();
      sourceCode += instanceName + "." + name + " = new " + type + "[" + size.str() + "];\n";
    }

    return sourceCode;
  }

  std::string buildReadAccess(llvm::StringRef targetRef, QualType type, std::string idx, llvm::StringRef fieldPath) {
    std::string source = getAccessToType(type, targetRef.str()) + "[" + idx + "].";

    std::vector<FieldDecl*> fieldDecls = resolveFieldDecl(getMostLikelyIterableType(type)->getAsRecordDecl(), fieldPath);
    for (int i = 0; i < fieldDecls.size() - 1; i++) {
      source = getAccessToType(fieldDecls[i]->getType(), source + fieldDecls[i]->getNameAsString()) + ".";
    }

    source += fieldDecls[fieldDecls.size() - 1]->getNameAsString();

    return source;
  }

  std::string buildReadAccess(llvm::StringRef targetRef, QualType type, llvm::StringRef fieldPath) {
    std::string source = getAccessToType(type, targetRef.str()) + ".";

    std::vector<FieldDecl*> fieldDecls = resolveFieldDecl(getMostLikelyIterableType(type)->getAsRecordDecl(), fieldPath);
    for (int i = 0; i < fieldDecls.size() - 1; i++) {
      source = getAccessToType(fieldDecls[i]->getType(), source + fieldDecls[i]->getNameAsString()) + ".";
    }

    source += fieldDecls[fieldDecls.size() - 1]->getNameAsString();

    return source;
  }

  std::string getSoaConversionForLoop(llvm::StringRef targetRef, QualType type, RecordDecl *decl, llvm::StringRef *fieldPaths, unsigned int fieldPathsCount, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "for (int " + idxName + " = 0; " + idxName + " < " + size.str() + "; " + idxName + "++) {\n";
    for (unsigned int i = 0; i < fieldPathsCount; i++) {
      std::string fieldName = getNestedFieldDecl(decl, fieldPaths[i])->getNameAsString();
      sourceCode += soaName + "." + fieldName + "[" + idxName + "] = " + buildReadAccess(targetRef, type, idxName, fieldPaths[i]) + ";\n";
    }
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getSoaConversionForRangeLoop(llvm::StringRef targetRef, QualType type, RecordDecl *decl, llvm::StringRef *fieldPaths, unsigned int fieldPathsCount, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto &element : " + targetRef.str() + ") {\n";
    for (unsigned int i = 0; i < fieldPathsCount; i++) {
      std::string fieldName = getNestedFieldDecl(decl, fieldPaths[i])->getNameAsString();
      sourceCode += soaName + "." + fieldName + "[" + idxName + "] = " + buildReadAccess("element", type, fieldPaths[i]) + ";\n";
    }
    sourceCode += idxName + "++;\n";
    sourceCode += "} }\n";
    return sourceCode;
  }

  std::string getSoaUnconversionForLoop(llvm::StringRef targetRef, QualType type, RecordDecl *decl, llvm::StringRef *fieldPaths, unsigned int fieldPathsCount, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "for (int " + idxName + " = 0; " + idxName + " < " + size.str() + "; " + idxName + "++) {\n";
    for (unsigned int i = 0; i < fieldPathsCount; i++) {
      std::string fieldName = getNestedFieldDecl(decl, fieldPaths[i])->getNameAsString();
      sourceCode += buildReadAccess(targetRef, type, idxName, fieldPaths[i]) + " = " + soaName + "." + fieldName + "[" + idxName + "];\n";
    }
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getSoaUnconversionForRangeLoop(llvm::StringRef targetRef, QualType type, RecordDecl *decl, llvm::StringRef *fieldPaths, unsigned int fieldPathsCount, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto &element : " + targetRef.str() + ") {\n";
    for (unsigned int i = 0; i < fieldPathsCount; i++) {
      std::string fieldName = getNestedFieldDecl(decl, fieldPaths[i])->getNameAsString();
      sourceCode += buildReadAccess("element", type, fieldPaths[i]) + " = " + soaName + "." + fieldName + "[" + idxName + "];\n";
    }
    sourceCode += idxName + "++;\n";
    sourceCode += "} }\n";
    return sourceCode;
  }

  class MemberExprRewriter : public ASTConsumer, public RecursiveASTVisitor<MemberExprRewriter> {

    FieldDecl *decl;
    std::string replacementExpr;

    Rewriter &R;

  public:

    MemberExprRewriter(Rewriter &R) : R(R) {}

    void replaceMemberExprs(FieldDecl *fDecl, std::string replacement, Stmt *context) {
      this->decl = fDecl;
      this->replacementExpr = replacement;
      this->TraverseStmt(context);
    }

    bool VisitMemberExpr(MemberExpr *E) {
      ValueDecl *valueDecl = E->getMemberDecl();
      if (valueDecl != decl) return true;
      SourceLocation start = E->getBeginLoc();
      SourceLocation end = E->getEndLoc();
      R.ReplaceText(SourceRange(start, end), replacementExpr);
      return true;
    }

  };

  std::string getForStmtIdx(ForStmt *S) {
    auto *initExpr = S->getInit();
    if (!llvm::isa<DeclStmt>(initExpr)) return "<not a DeclStmt>";
    auto *declStmt = llvm::cast<DeclStmt>(initExpr);
    if (!declStmt->isSingleDecl()) return "<not a single decl>";
    auto *varDecl = llvm::cast<VarDecl>(declStmt->getSingleDecl());
    std::string name = varDecl->getNameAsString();
    return name;
  }

  SourceRange getStmtRangeInclAttrs(Stmt *S) {
    auto parent = CI.getASTContext().getParentMapContext().getParents(*S)[0];
    if (parent.getNodeKind().KindId != ASTNodeKind::NodeKindId::NKI_AttributedStmt) return S->getSourceRange();
    auto *genericStmt = parent.get<Stmt>();
    return genericStmt->getSourceRange();
  }

  void writeBeforeForStmt(Stmt *S, std::string s) {
     SourceLocation beforeStmt = getStmtRangeInclAttrs(S).getBegin();
     R.InsertTextBefore(beforeStmt, s);
  }

  void writeAfterStmt(Stmt *S, std::string s) {
     SourceLocation afterStmt = getStmtRangeInclAttrs(S).getEnd();
     R.InsertTextAfterToken(afterStmt, s);
  }

public:
  SoaConversionASTConsumer(CompilerInstance &CI) : CI(CI), R(CI.getSourceManager().getRewriter()) {}

  bool VisitForStmt(ForStmt *S) {
    const SoaConversionInputsAttr *conversionInputsAttr = getAttr<SoaConversionInputsAttr>(S);
    const SoaConversionOutputsAttr *conversionOutputsAttr = getAttr<SoaConversionOutputsAttr>(S);
    const SoaConversionTargetAttr *conversionTargetAttr = getAttr<SoaConversionTargetAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);

    if (!conversionInputsAttr || !conversionOutputsAttr || !conversionTargetAttr || !conversionTargetSizeAttr) return true;

    FunctionDecl *loopParent = getLoopParentFunctionDecl(S);
    auto *targetDeclRef = getTargetDeclRefExpr(conversionTargetAttr->getTargetRef(), loopParent);
    RecordDecl *targetRecordDecl = getTargetRecordDecl(targetDeclRef, loopParent);
    std::vector<FieldDecl*> soaFields = getFieldsForSoa(targetRecordDecl, conversionInputsAttr->inputFields_begin(), conversionInputsAttr->inputFields_size());

    std::string soaDef = getSoaDef(conversionTargetAttr->getTargetRef(), soaFields, conversionTargetSizeAttr->getTargetSizeExpr());
    std::string soaConv = getSoaConversionForLoop(
        conversionTargetAttr->getTargetRef(), targetDeclRef->getType(), targetRecordDecl,
        conversionInputsAttr->inputFields_begin(), conversionInputsAttr->inputFields_size(),
        conversionTargetSizeAttr->getTargetSizeExpr());
    writeBeforeForStmt(S, soaDef + "\n" + soaConv + "\n");

    std::string soaUnconv = getSoaUnconversionForLoop(
        conversionTargetAttr->getTargetRef(), targetDeclRef->getType(), targetRecordDecl,
        conversionOutputsAttr->outputFields_begin(), conversionOutputsAttr->outputFields_size(),
        conversionTargetSizeAttr->getTargetSizeExpr());
    writeAfterStmt(S, "\n" + soaUnconv);

    MemberExprRewriter rewriter(R);
    std::string forLoopIdx = getForStmtIdx(S);
    for (unsigned long i = 0; i < soaFields.size(); i++) {
      std::string replacement = conversionTargetAttr->getTargetRef().str() + "__SoA__instance" + "." + soaFields[i]->getNameAsString() + "[" + forLoopIdx + "]" ;
      rewriter.replaceMemberExprs(soaFields[i], replacement, S);
    }
    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
    const SoaConversionInputsAttr *conversionInputsAttr = getAttr<SoaConversionInputsAttr>(S);
    const SoaConversionOutputsAttr *conversionOutputsAttr = getAttr<SoaConversionOutputsAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);

    if (!conversionInputsAttr || !conversionOutputsAttr || !conversionTargetSizeAttr) return true;

    std::string targetRefStr = R.getRewrittenText(S->getRangeInit()->getSourceRange());
    llvm::StringRef targetRef = llvm::StringRef(targetRefStr);

    RecordDecl *targetRecordDecl = getMostLikelyIterableType(S->getLoopVariable()->getType())->getAsRecordDecl();
    std::vector<FieldDecl*> soaFields = getFieldsForSoa(targetRecordDecl, conversionInputsAttr->inputFields_begin(), conversionInputsAttr->inputFields_size());

    std::string soaDef = getSoaDef(targetRef, soaFields, conversionTargetSizeAttr->getTargetSizeExpr());
    std::string soaConv = getSoaConversionForRangeLoop(
        targetRef, S->getLoopVariable()->getType(), targetRecordDecl,
        conversionInputsAttr->inputFields_begin(), conversionInputsAttr->inputFields_size(),
        conversionTargetSizeAttr->getTargetSizeExpr());
    writeBeforeForStmt(S, soaDef + "\n" + soaConv + "\n");

    std::string soaUnconv = getSoaUnconversionForRangeLoop(
        targetRef, S->getLoopVariable()->getType(), targetRecordDecl,
        conversionInputsAttr->inputFields_begin(), conversionInputsAttr->inputFields_size(),
        conversionTargetSizeAttr->getTargetSizeExpr());
    writeAfterStmt(S, "\n" + soaUnconv);

    std::string forLoopIdx = targetRefStr + "__main_loop_iter";

    SourceRange newForLoop(S->getSourceRange().getBegin(), S->getBody()->getBeginLoc().getLocWithOffset(-1));
    R.ReplaceText(newForLoop, "for (unsigned int " + forLoopIdx + " = 0; " + forLoopIdx + " < " + conversionTargetSizeAttr->getTargetSizeExpr().str() + "; " + forLoopIdx + "++)");

    MemberExprRewriter rewriter(R);
    for (unsigned long i = 0; i < soaFields.size(); i++) {
      std::string replacement = targetRef.str() + "__SoA__instance" + "." + soaFields[i]->getNameAsString() + "[" + forLoopIdx + "]" ;
      rewriter.replaceMemberExprs(soaFields[i], replacement, S);
    }
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

};

#endif // CLANG_SOACONVERSIONASTCONSUMER_H
