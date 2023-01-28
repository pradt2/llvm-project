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

  template<typename Attr>
  const std::vector<const Attr*> getAttrs(Stmt *S) {
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
      std::vector<const Attr*> attrs;
      if (!stmt) return attrs;
      for (auto *attr : stmt->getAttrs()) {
        if (!llvm::isa<Attr>(attr)) continue;
        attrs.push_back(llvm::cast<Attr>(attr));
      }
      return attrs;
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

  QualType getTypeOfExpr(RecordDecl *decl, llvm::StringRef path) {

    std::vector<std::string> fragments = splitString(path, ".");
    for (size_t i = 0; i < fragments.size(); i++) {
      auto fragment = fragments[i];

      QualType itemType;

      if (llvm::StringRef(fragment).endswith("()")) {
        // it's a method call

        auto methodName = fragment;
        methodName.pop_back();
        methodName.pop_back(); // remove the '()'

        if (!llvm::isa<CXXRecordDecl>(decl)) {
          // method call on a declaration that cannot have methods
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Method call on a declaration type that does not allow methods";
          exit(1);
        }

        auto *cxxRecordDecl = llvm::cast<CXXRecordDecl>(decl);

        CXXMethodDecl *methodDecl = nullptr;
        for (auto *method : cxxRecordDecl->methods()) {
          if (method->getNameAsString() != methodName) continue; // TODO what about inherited methods?
          methodDecl = method; // TODO what about overloads?
          break;
        }

        if (methodDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find method: " << methodName;
          exit(1);
        }

        itemType = methodDecl->getType();
      } else {
        // it's a field

        FieldDecl *fieldDecl = nullptr;
        for (auto *field : decl->fields()) {
          if (field->getNameAsString() != fragment) continue ;
          fieldDecl = field;
          break ;
        }

        if (fieldDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find field: " << fragment;
          exit(1);
        }

        itemType = fieldDecl->getType();
      }

      // if this is the last item (no more .field or .method() accesses, then return it)
      if (i == fragments.size() - 1) return itemType;

      // it isn't the last item, so it must be RecordDecl to have fields or methods
      if (!itemType->isRecordType()) {
        llvm::errs() << __FILE__ << ":" << __LINE__ << " Type of " << fragment << " is not a RecordType";
        exit(1);
      }

      decl = itemType->getAsRecordDecl();
    }

    llvm::errs() << __FILE__ << ":" << __LINE__ << " Cannot determine expression type";
    exit(1);
  }

  struct SoaField {
    std::string name;
    QualType type;
    llvm::StringRef inputAccessPath;
    llvm::StringRef outputAccessPath;
  };

  std::vector<SoaField> getFieldsForSoa(RecordDecl *decl, std::vector<const SoaConversionDataItemAttr*> conversionDataAttrs) {
    std::vector<SoaField> fields;

    int i = 0;
    for (auto *item : conversionDataAttrs) {

      auto type = getTypeOfExpr(decl, item->getInput());
      std::string name = "_" + std::to_string(i);

      fields.push_back({
          name,
          type,
          item->getInput(),
          item->getOutput()
      });

      i++;
    }
    return fields;
  }

  std::string getSoaDef(llvm::StringRef targetRef, std::vector<SoaField> soaFields, llvm::StringRef size, ASTContext &Ctx) {
    std::string instanceName = targetRef.str() + "__SoA__instance";
    std::string sourceCode = "struct " + targetRef.str() + "__SoA {\n";
    for (auto field : soaFields) {
      auto type = toSource(*fromQualType(field.type, Ctx));
      sourceCode += "    " + type + " * " + "(" + field.name + ");\n";
    }
    sourceCode += "\n} " + instanceName + ";\n\n";

    for (auto field : soaFields) {
      auto type = toSource(*fromQualType(field.type, Ctx));
      sourceCode += instanceName + "." + field.name + " = new " + type + "[" + size.str() + "];\n";
    }

    return sourceCode;
  }

  std::string buildReadAccess(llvm::StringRef targetRef, QualType type, std::string idx, llvm::StringRef accessPath) {
    // always ends in such a way that it's accessible via the '.' syntax
    // i.e. all pointers are always dereferenced
    auto source = targetRef.str() + "[" + idx + "]";

    return buildReadAccess(llvm::StringRef(source), type, accessPath);
  }

  std::string buildReadAccess(llvm::StringRef targetRef, QualType type, llvm::StringRef accessPath) {
    std::string source = targetRef.str();

    auto *decl = getMostLikelyIterableType(type)->getAsRecordDecl();

    std::vector<std::string> fragments = splitString(accessPath, ".");
    for (size_t i = 0; i < fragments.size(); i++) {
      auto fragment = fragments[i];

      QualType itemType;

      if (llvm::StringRef(fragment).endswith("()")) {
        // it's a method call

        auto methodName = fragment;
        methodName.pop_back();
        methodName.pop_back(); // remove the '()'

        if (!llvm::isa<CXXRecordDecl>(decl)) {
          // method call on a declaration that cannot have methods
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Method call on a declaration type that does not allow methods";
          exit(1);
        }

        auto *cxxRecordDecl = llvm::cast<CXXRecordDecl>(decl);

        CXXMethodDecl *methodDecl = nullptr;
        for (auto *method : cxxRecordDecl->methods()) {
          if (method->getNameAsString() != methodName) continue; // TODO what about inherited methods?
          methodDecl = method; // TODO what about overloads?
          break;
        }

        if (methodDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find method: " << methodName;
          exit(1);
        }

        itemType = methodDecl->getType();
        source += "." + fragment;
        source = getAccessToType(itemType, source);

      } else {
        // it's a field

        FieldDecl *fieldDecl = nullptr;
        for (auto *field : decl->fields()) {
          if (field->getNameAsString() != fragment) continue ;
          fieldDecl = field;
          break ;
        }

        if (fieldDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find field: " << fragment;
          exit(1);
        }

        itemType = fieldDecl->getType();
        source += "." + fragment;
        source = getAccessToType(itemType, source);
      }

      // if this is the last item (no more .field or .method() accesses, then return it)
      if (i == fragments.size() - 1) return source;

      // it isn't the last item, so it must be RecordDecl to have fields or methods
      if (!itemType->isRecordType()) {
        llvm::errs() << __FILE__ << ":" << __LINE__ << " Type of " << fragment << " is not a RecordType";
        exit(1);
      }

      decl = itemType->getAsRecordDecl();
    }

    llvm::errs() << __FILE__ << ":" << __LINE__ << " Cannot determine expression type";
    exit(1);
  }

  std::string buildWriteStmt(llvm::StringRef targetRef, QualType type, llvm::StringRef accessPath, std::string newValue) {
    std::string source = targetRef.str();

    auto *decl = getMostLikelyIterableType(type)->getAsRecordDecl();

    std::vector<std::string> fragments = splitString(accessPath, ".");
    for (size_t i = 0; i < fragments.size(); i++) {
      auto fragment = fragments[i];

      QualType itemType;

      bool lastItemMethod = false;

      if (llvm::StringRef(fragment).endswith("()")) {
        // it's a method call

        auto methodName = fragment;
        methodName.pop_back();
        methodName.pop_back(); // remove the '()'

        if (!llvm::isa<CXXRecordDecl>(decl)) {
          // method call on a declaration that cannot have methods
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Method call on a declaration type that does not allow methods";
          exit(1);
        }

        auto *cxxRecordDecl = llvm::cast<CXXRecordDecl>(decl);

        CXXMethodDecl *methodDecl = nullptr;
        for (auto *method : cxxRecordDecl->methods()) {
          if (method->getNameAsString() != methodName) continue; // TODO what about inherited methods?
          methodDecl = method; // TODO what about overloads?
          break;
        }

        if (methodDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find method: " << methodName;
          exit(1);
        }

        itemType = methodDecl->getType();
        source += "." + fragment;
        source = getAccessToType(itemType, source);
        lastItemMethod = true;
      } else {
        // it's a field

        FieldDecl *fieldDecl = nullptr;
        for (auto *field : decl->fields()) {
          if (field->getNameAsString() != fragment) continue ;
          fieldDecl = field;
          break ;
        }

        if (fieldDecl == nullptr) {
          llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find field: " << fragment;
          exit(1);
        }

        itemType = fieldDecl->getType();
        source += "." + fragment;
        source = getAccessToType(itemType, source);
        lastItemMethod = false;
      }

      // if this is the last item (no more .field or .method() accesses)
      if (i == fragments.size() - 1) {
        if (!lastItemMethod) return source + " = " + newValue + ";";

        source.pop_back(); // remove ')'

        return source + newValue + ");";
      }

      // it isn't the last item, so it must be RecordDecl to have fields or methods
      if (!itemType->isRecordType()) {
        llvm::errs() << __FILE__ << ":" << __LINE__ << " Type of " << fragment << " is not a RecordType";
        exit(1);
      }

      decl = itemType->getAsRecordDecl();
    }

    llvm::errs() << __FILE__ << ":" << __LINE__ << " Cannot determine expression type";
    exit(1);
  }

  std::string getSoaConversionForLoop(llvm::StringRef targetRef, QualType type, std::vector<SoaField> soaFields, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "for (int " + idxName + " = 0; " + idxName + " < " + size.str() + "; " + idxName + "++) {\n";
    for (auto field: soaFields) {
      std::string fieldName = field.name;
      sourceCode += soaName + "." + fieldName + "[" + idxName + "] = " + buildReadAccess(targetRef, type, idxName, field.inputAccessPath) + ";\n";
    }
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getSoaConversionForRangeLoop(llvm::StringRef targetRef, QualType type, std::vector<SoaField> soaFields) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto &element : " + targetRef.str() + ") {\n";
    for (auto field: soaFields) {
      std::string fieldName = field.name;
      sourceCode += soaName + "." + fieldName + "[" + idxName + "] = " + buildReadAccess("element", type, field.inputAccessPath) + ";\n";
    }
    sourceCode += idxName + "++;\n";
    sourceCode += "} }\n";
    return sourceCode;
  }

  std::string getSoaUnconversionForLoop(llvm::StringRef targetRef, QualType type, std::vector<SoaField> soaFields, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "for (int " + idxName + " = 0; " + idxName + " < " + size.str() + "; " + idxName + "++) {\n";
    for (auto field: soaFields) {
      std::string fieldName = field.name;
      sourceCode = buildWriteStmt(targetRef, type, field., soaName + "." + fieldName + "[" + idxName + "]") + "\n";
    }
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getSoaUnconversionForRangeLoop(llvm::StringRef targetRef, QualType type, std::vector<std::tuple<QualType, std::string>> typesNames, llvm::StringRef *accessPaths, unsigned int accessPathsCount, llvm::StringRef size) {
    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto &element : " + targetRef.str() + ") {\n";
    for (unsigned int i = 0; i < accessPathsCount; i++) {
      std::string fieldName = std::get<std::string>(typesNames[i]);
      sourceCode += buildWriteStmt("element", type, accessPaths[i], soaName + "." + fieldName + "[" + idxName + "]") + "\n";
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
    const std::vector<const SoaConversionDataItemAttr*> conversionDataAttrs = getAttrs<SoaConversionDataItemAttr>(S);
    const SoaConversionTargetAttr *conversionTargetAttr = getAttr<SoaConversionTargetAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);

    if (conversionDataAttrs.empty() || !conversionTargetAttr || !conversionTargetSizeAttr) return true;

    FunctionDecl *loopParent = getLoopParentFunctionDecl(S);
    auto *targetDeclRef = getTargetDeclRefExpr(conversionTargetAttr->getTargetRef(), loopParent);
    RecordDecl *targetRecordDecl = getTargetRecordDecl(targetDeclRef, loopParent);
    std::vector<std::tuple<QualType, std::string>> soaFields = getFieldsForSoa(targetRecordDecl, conversionDataAttrs);

    ASTContext &Ctx = loopParent->getASTContext();

    std::string soaDef = getSoaDef(conversionTargetAttr->getTargetRef(), soaFields, conversionTargetSizeAttr->getTargetSizeExpr(), Ctx);
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
