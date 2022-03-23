#ifndef CLANG_SOATRANSFORMGENERATOR_H
#define CLANG_SOATRANSFORMGENERATOR_H

class SoaTransformGenerator : public ASTConsumer, public RecursiveASTVisitor<SoaTransformGenerator> {

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

  RecordDecl *getTargetRecordDecl(llvm::StringRef targetRef, FunctionDecl *loopParent) {
    auto *declRef = getTargetDeclRefExpr(targetRef, loopParent);
    auto type = declRef->getDecl()->getType();
    if (type->isRecordType()) return nullptr;
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

  FieldDecl *getNestedFieldDecl(RecordDecl *decl, llvm::StringRef fieldPath) {
    std::vector<std::string> fieldPathNames = splitString(fieldPath, ".");
    bool foundDecl;
    FieldDecl *fieldDecl = nullptr;
    for (unsigned i = 0; i < fieldPathNames.size(); i++) {
      auto fieldName = fieldPathNames[i];
      foundDecl = false;
      for (auto *field : decl->fields()) {
        if (field->getName() != fieldName) continue;
        fieldDecl = field;
        QualType fieldType = field->getType();
        if (fieldType->isRecordType()) {
          decl = fieldType->getAsRecordDecl();
          foundDecl = true;
          break;
        }
        if (i == fieldPathNames.size() - 1) {
          foundDecl = true;
          break;
        }
        llvm::errs() << "Nested non-leaf field type is not Record " << __FILE__ << ":" << __LINE__ << "\n";
        return nullptr;
      }
      if (!foundDecl) {
        llvm::errs() << "Could not find field " << __FILE__ << ":" << __LINE__ << "\n";
        return nullptr;
      }
    }
    return fieldDecl;
  }

  std::vector<FieldDecl *> getFieldsForSoa(RecordDecl *decl, llvm::StringRef fields) {
    std::vector<std::string> fieldsList = splitString(fields, ",");
    std::vector<FieldDecl *> foundFields;
    for (auto field : fieldsList) {
      auto *newField = getNestedFieldDecl(decl, field);
      foundFields.push_back(newField);
    }
    return foundFields;
  }

  std::string getSoaDef(llvm::StringRef targetRef, std::vector<FieldDecl *> fromFieldDecls) {
    std::string sourceCode = "struct " + targetRef.str() + "__SoA {\n";
    for (auto *field : fromFieldDecls) {
      auto semaDecl = fromFieldDecl(field);
      std::string decl = toSource(*semaDecl);
    }
  }

public:
  SoaTransformGenerator(CompilerInstance &CI) : CI(CI), R(*CI.getSourceManager().getRewriter()) {}

  bool VisitForStmt(ForStmt *S) {
    const SoaConversionAttr *conversionAttr = getAttr<SoaConversionAttr>(S);
    const SoaConversionTargetAttr *conversionTargetAttr = getAttr<SoaConversionTargetAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }
};

#endif // CLANG_SOATRANSFORMGENERATOR_H
