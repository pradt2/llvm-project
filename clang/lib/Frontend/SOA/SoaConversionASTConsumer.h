#ifndef CLANG_SOACONVERSIONASTCONSUMER_H
#define CLANG_SOACONVERSIONASTCONSUMER_H

#include "../SemaIR/SemaIR.h"
#include "../Compression/Utils.h"

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

  std::tuple<Decl*, QualType, long> getTypeOfExpr(RecordDecl *decl, llvm::StringRef path, bool isOutput = false) {

    std::vector<std::string> fragments = splitString(path, ".");
    for (size_t i = 0; i < fragments.size(); i++) {
      auto fragment = fragments[i];

      QualType itemType;
      Decl *itemDecl = nullptr;
      long literalIdx = -1;

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

        itemType = methodDecl->getReturnType();
        itemDecl = methodDecl;
      } else if (llvm::StringRef(fragment).contains('[') && llvm::StringRef(fragment).endswith("]")) {
        // it's an array access

        auto openBracketIdx = llvm::StringRef(fragment).find('[');
        auto closedBracketIdx = fragment.size() - 1;
        auto idxLiteralStr = llvm::StringRef(fragment).substr(openBracketIdx + 1, closedBracketIdx - openBracketIdx - 1);
        auto idxLiteralValue = std::stoul(idxLiteralStr.str());
        auto fieldName = llvm::StringRef(fragment).substr(0, openBracketIdx);

          FieldDecl *fieldDecl = nullptr;
          for (auto *field : decl->fields()) {
              if (field->getNameAsString() != fieldName) continue ;
              fieldDecl = field;
              break ;
          }

          if (fieldDecl == nullptr) {
              llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find array access: " << fieldName << " idx: " << idxLiteralValue;
              exit(1);
          }

          itemType = fieldDecl->getType()->getAsArrayTypeUnsafe()->getElementType();
          itemDecl = fieldDecl;
          literalIdx = idxLiteralValue;
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
        itemDecl = fieldDecl;
      }

      // if this is the last item (no more .field or .method() accesses, then return it)
      if (i == fragments.size() - 1) {
        if (llvm::isa<FieldDecl>(itemDecl) || !isOutput) return std::make_tuple(itemDecl, itemType, literalIdx);

        // if it's output, and we have a method decl, then the type isn't the return type (that will always be void for setters I HOPE)
        // but it is the type of the (only, I HOPE) argument that the setter method accepts

        auto *methodDecl = llvm::cast<CXXMethodDecl>(itemDecl);
        if (methodDecl->param_size() != 1) {
          llvm::errs() << __LINE__ << ":" << __FILE__ << " Setter method accepts n != 1 arguments";
          exit(1);
        }

        itemType = methodDecl->parameters()[0]->getType();

        return std::make_tuple(itemDecl, itemType, literalIdx);
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

  struct SoaField {
    std::string name;
    QualType type;
    long literalIdx;
    llvm::StringRef inputAccessPath;
    llvm::StringRef outputAccessPath;
    Decl *inputAccessDecl;
    Decl *outputAccessDecl;
  };

  std::vector<SoaField> getFieldsForSoa(RecordDecl *decl, std::vector<const SoaConversionDataItemAttr*> conversionDataAttrs) {
    std::vector<SoaField> fields;

    int i = -1;
    for (auto *item : conversionDataAttrs) {
      i++; // it's better kept here so that I don't have to replicate it on every 'continue;'

      auto soaField = SoaField();
      soaField.name = "_" + std::to_string(i);

      auto inputDeclType = getTypeOfExpr(decl, item->getInput());
      soaField.inputAccessPath = item->getInput();
      soaField.inputAccessDecl = std::get<Decl*>(inputDeclType);
      soaField.type = std::get<QualType>(inputDeclType);
      soaField.outputAccessPath = item->getOutput();
      soaField.literalIdx = std::get<long>(inputDeclType);

      if (soaField.outputAccessPath.empty()) {
        fields.push_back(soaField);
        continue;
      }

      auto outputDeclType = getTypeOfExpr(decl, item->getOutput(), true);
      auto outputType = std::get<QualType>(outputDeclType);

      // if the setter accepts a reference to an otherwise compatible type, allow it
      if (outputType->isReferenceType()) {
        auto *refType = outputType->getAs<ReferenceType>();

        // also ignore 'const' etc.
        outputType = refType->getPointeeType().getUnqualifiedType();
      }

      // TODO look into this tarch::la::vector get , set(const tarch::la::vector &val)
//      if (std::get<QualType>(inputDeclType).getUnqualifiedType() != outputType) {
//        // TODO what about inheritance?
//        llvm::errs() << __FILE__ << ":" << __LINE__ << "Input type and output type do not match up";
//        exit(1);
//      }

      soaField.outputAccessDecl = std::get<Decl*>(outputDeclType);

      fields.push_back(soaField);
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

  std::string getSoaFreeStmts(llvm::StringRef targetRef, std::vector<SoaField> soaFields) {
      std::string instanceName = targetRef.str() + "__SoA__instance";

      std::string sourceCode;

      auto reverseSoaFields = std::vector<SoaField>(soaFields.rbegin(), soaFields.rend());
      for (auto &field : reverseSoaFields) {
          sourceCode += "delete[] " + instanceName + "." + field.name + ";\n";
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

        itemType = methodDecl->getReturnType();
        source += "." + fragment;
        source = getAccessToType(itemType, source);

      } else if (llvm::StringRef(fragment).contains('[') && llvm::StringRef(fragment).endswith("]")) {
          // it's an array access

          auto openBracketIdx = llvm::StringRef(fragment).find('[');
          auto closedBracketIdx = fragment.size() - 1;
          auto idxLiteralStr = llvm::StringRef(fragment).substr(openBracketIdx + 1, closedBracketIdx - openBracketIdx - 1);
          auto idxLiteralValue = std::stoul(idxLiteralStr.str());
          auto fieldName = llvm::StringRef(fragment).substr(0, openBracketIdx);

          FieldDecl *fieldDecl = nullptr;
          for (auto *field : decl->fields()) {
              if (field->getNameAsString() != fieldName) continue ;
              fieldDecl = field;
              break ;
          }

          if (fieldDecl == nullptr) {
              llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find array access: " << fieldName << " idx: " << idxLiteralValue;
              exit(1);
          }

          itemType = fieldDecl->getType()->getAsArrayTypeUnsafe()->getElementType();
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

  std::string buildWriteStmt(llvm::StringRef targetRef, QualType type, std::string idx, llvm::StringRef accessPath, std::string newValue) {
    // always ends in such a way that it's accessible via the '.' syntax
    // i.e. all pointers are always dereferenced
    auto source = targetRef.str() + "[" + idx + "]";

    return buildWriteStmt(llvm::StringRef(source), type, accessPath, newValue);
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
              llvm::errs() << __FILE__ << ":" << __LINE__
                           << " Method call on a declaration type that does not allow methods";
              exit(1);
          }

          auto *cxxRecordDecl = llvm::cast<CXXRecordDecl>(decl);

          CXXMethodDecl *methodDecl = nullptr;
          for (auto *method: cxxRecordDecl->methods()) {
              if (method->getNameAsString() != methodName) continue; // TODO what about inherited methods?
              methodDecl = method; // TODO what about overloads?
              break;
          }

          if (methodDecl == nullptr) {
              llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find method: " << methodName;
              exit(1);
          }

          itemType = methodDecl->getReturnType();
          source += "." + fragment;
          source = getAccessToType(itemType, source);
          lastItemMethod = true;
      } else if (llvm::StringRef(fragment).contains('[') && llvm::StringRef(fragment).endswith("]")) {
          // it's an array access

          auto openBracketIdx = llvm::StringRef(fragment).find('[');
          auto closedBracketIdx = fragment.size() - 1;
          auto idxLiteralStr = llvm::StringRef(fragment).substr(openBracketIdx + 1,
                                                                closedBracketIdx - openBracketIdx - 1);
          auto idxLiteralValue = std::stoul(idxLiteralStr.str());
          auto fieldName = llvm::StringRef(fragment).substr(0, openBracketIdx);

          FieldDecl *fieldDecl = nullptr;
          for (auto *field: decl->fields()) {
              if (field->getNameAsString() != fieldName) continue;
              fieldDecl = field;
              break;
          }

          if (fieldDecl == nullptr) {
              llvm::errs() << __FILE__ << ":" << __LINE__ << " Could not find array access: " << fieldName << " idx: "
                           << idxLiteralValue;
              exit(1);
          }

          itemType = fieldDecl->getType()->getAsArrayTypeUnsafe()->getElementType();
          source += "." + fragment;
          source = getAccessToType(itemType, source);
          lastItemMethod = false;
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
    std::string elementDerefAccessor = getAccessToType(type, "element");

    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto element : " + targetRef.str() + ") {\n";
    for (auto field: soaFields) {
      std::string fieldName = field.name;
      sourceCode += soaName + "." + fieldName + "[" + idxName + "] = " + buildReadAccess(elementDerefAccessor, type, field.inputAccessPath) + ";\n";
    }
    sourceCode += idxName + "++;\n";
    sourceCode += "} }\n";
    return sourceCode;
  }

  int getRWSoaFields(std::vector<SoaField> &soaFields) {
    int count = 0;
    for (auto field: soaFields) {
      if (!field.outputAccessDecl) continue ;

      count++;
    }
    return count;
  }

  std::string getSoaUnconversionForLoop(llvm::StringRef targetRef, QualType type, std::vector<SoaField> soaFields, llvm::StringRef size) {
    if (getRWSoaFields(soaFields) == 0) return "/** SOA epilogue loop skipped for target " + targetRef.str() + ": no items to write back */\n";

    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string sourceCode = "for (int " + idxName + " = 0; " + idxName + " < " + size.str() + "; " + idxName + "++) {\n";
    for (auto field: soaFields) {
      if (field.outputAccessPath.empty()) continue ; // skip copying fields that are supposed to be read only
      std::string fieldName = field.name;
      sourceCode += buildWriteStmt(targetRef, type, idxName, field.outputAccessPath, soaName + "." + fieldName + "[" + idxName + "]") + "\n";
    }
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getSoaUnconversionForRangeLoop(llvm::StringRef targetRef, QualType type, std::vector<SoaField> soaFields) {
    if (getRWSoaFields(soaFields) == 0) return "/** SOA epilogue loop skipped for target " + targetRef.str() + ": no items to write back */\n";

    std::string soaName = targetRef.str() + "__SoA__instance";
    std::string idxName = targetRef.str() + "__SoA__instance__iter";
    std::string elementDerefAccessor = getAccessToType(type, "element");

    std::string sourceCode = "{ unsigned int " + idxName + " = 0;\n";
    sourceCode += "for (auto element : " + targetRef.str() + ") {\n";
    for (auto field: soaFields) {
      if (field.outputAccessPath.empty()) continue ; // skip copying fields that are supposed to be read only
      std::string fieldName = field.name;
      sourceCode += buildWriteStmt(elementDerefAccessor, type, field.outputAccessPath, soaName + "." + fieldName + "[" + idxName + "]") + "\n";
    }
    sourceCode += idxName + "++;\n";
    sourceCode += "} }\n";
    return sourceCode;
  }

  class DeclRefExprVisitor : public ASTConsumer, public RecursiveASTVisitor<DeclRefExprVisitor> {
    std::string ident;
    bool hasBeenFound = false;

  public:
    explicit DeclRefExprVisitor(std::string ident) : ident(ident) {}

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      if (E->getDecl()->getNameAsString() == ident) {
        hasBeenFound = true;
        return false;
      }

      return true;
    }

    static bool doesExprBelongToIdent(std::string ident, Expr *E) {
      auto visitor = DeclRefExprVisitor(ident);
      visitor.TraverseStmt(E);
      bool hasBeenFound = visitor.hasBeenFound;
      return hasBeenFound;
    }
  };

  class MemberExprRewriter : public ASTConsumer, public RecursiveASTVisitor<MemberExprRewriter> {

    FieldDecl *decl;
    std::string replacementExpr;
    std::string parentVariableIdent;
    Rewriter &R;

  public:

    MemberExprRewriter(Rewriter &R) : R(R) {}

    void replaceMemberExprs(FieldDecl *fDecl, std::string parentVariableIdent, std::string replacement, Stmt *context) {
      this->decl = fDecl;
      this->replacementExpr = replacement;
      this->parentVariableIdent = parentVariableIdent;
      this->TraverseStmt(context);
    }

    bool VisitMemberExpr(MemberExpr *E) {
      ValueDecl *valueDecl = E->getMemberDecl();
      if (valueDecl != decl) return true;

      // avoid cases where the desired field is accessed on an object of the same type,
      // but not of a member of the variable that is an element of the iterated collection
      // if there is a nested loop where the object type of the inner loop is the same as of the outer loop
      // problems occur unless we do this check
      if (!DeclRefExprVisitor::doesExprBelongToIdent(parentVariableIdent, E)) return true;

      SourceLocation start = E->getBeginLoc();
      SourceLocation end = E->getEndLoc();
      R.ReplaceText(SourceRange(start, end), replacementExpr);
      return true;
    }

  };

  class ArraySubscriptExprRewriter : public ASTConsumer, public RecursiveASTVisitor<ArraySubscriptExprRewriter> {
      FieldDecl *decl;
      std::string replacementExpr;
      std::string parentVariableIdent;
      long subscriptLiteralValue;
      Rewriter &R;

  public:
      ArraySubscriptExprRewriter(Rewriter &R) : R(R) {}

      void replaceArraySubscriptExprs(FieldDecl *fDecl, std::string parentVariableIdent, std::string replacement, long subscriptLiteralValue, Stmt *context) {
          this->decl = fDecl;
          this->replacementExpr = replacement;
          this->parentVariableIdent = parentVariableIdent;
          this->subscriptLiteralValue = subscriptLiteralValue;
          this->TraverseStmt(context);
      }

      bool VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
          Expr::EvalResult evalResult;
          bool idxEvalSuccess = E->getIdx()->EvaluateAsInt(evalResult, this->decl->getASTContext());
          if (!idxEvalSuccess) return true;

          long idxValue = evalResult.Val.getInt().getExtValue();
          if (idxValue != this->subscriptLiteralValue) return true;

          Expr *base = E->getBase();
          if (llvm::isa<ImplicitCastExpr>(base)) base = llvm::cast<ImplicitCastExpr>(base)->getSubExpr();
          if (!llvm::isa<MemberExpr>(base)) return true;
          MemberExpr *memberBase = llvm::cast<MemberExpr>(base);

          if (memberBase->getMemberDecl() != this->decl) return true;

          SourceLocation start = E->getBeginLoc();
          SourceLocation end = E->getEndLoc();
          R.ReplaceText(SourceRange(start, end), this->replacementExpr);

          return true;
      }
  };

  class CXXMemberCallExprRewriter : public ASTConsumer, public RecursiveASTVisitor<CXXMemberCallExprRewriter> {

    CXXMethodDecl *decl;
    std::string replacementExpr;
    std::string parentVariableIdent;
    Rewriter &R;

  public:

    CXXMemberCallExprRewriter(Rewriter &R) : R(R) {}

    void replaceMemberExprs(CXXMethodDecl *fDecl, std::string parentVariableIdent, std::string replacement, Stmt *context) {
      this->decl = fDecl;
      this->replacementExpr = replacement;
      this->parentVariableIdent = parentVariableIdent;
      this->TraverseStmt(context);
    }

    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
      ValueDecl *valueDecl = E->getMethodDecl();
      if (valueDecl != decl) return true;

      // avoid cases where the desired function is called on an object of the same type,
      // but not of a member of the variable that is an element of the iterated collection
      // if there is a nested loop where the object type of the inner loop is the same as of the outer loop
      // problems occur unless we do this check
      // we use getCallee() here to access the member expression and omit arguments, which would have
      // been traversed if we used E directly
      if (!DeclRefExprVisitor::doesExprBelongToIdent(parentVariableIdent, E->getCallee())) return true;

      if (decl->param_empty()) {
        // no arguments, we can replace the entire call expr (so incl. '()')
        // also, this style is only for getting data (not setting), so no '=' handling is necessary
        SourceLocation start = E->getBeginLoc();
        SourceLocation end = E->getEndLoc();
        R.ReplaceText(SourceRange(start, end), replacementExpr);
        return true;
      }

      // we try to preserve arguments here since the method is likely a setter
      SourceLocation start = E->getBeginLoc();
      SourceLocation end = E->getExprLoc().getLocWithOffset(decl->getName().size()).getLocWithOffset(-1); // preserve '('

      R.ReplaceText(SourceRange(start, end), replacementExpr + " = ");

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

  SourceRange getStmtRangeInclAttrs(const Stmt *S) {
    auto parent = CI.getASTContext().getParentMapContext().getParents(*S)[0];
    if (parent.getNodeKind().KindId != ASTNodeKind::NodeKindId::NKI_AttributedStmt) return S->getSourceRange();
    auto *genericStmt = parent.get<Stmt>();
    return genericStmt->getSourceRange();
  }

  void writeBeforeForStmt(const Stmt *S, std::string s) {
     SourceLocation beforeStmt = getStmtRangeInclAttrs(S).getBegin();
     R.InsertTextBefore(beforeStmt, s);
  }

  void writeAfterStmt(const Stmt *S, std::string s) {
     SourceLocation afterStmt = getStmtRangeInclAttrs(S).getEnd();
     R.InsertTextAfterToken(afterStmt, s);
  }

  void writePrologueAndEpilogue(Stmt *S, ASTContext &Ctx, std::string prologue, std::string epilogue) {
     const SoaConversionDataMovementStrategyAttr *strategyAttr = getAttr<SoaConversionDataMovementStrategyAttr>(S);
     auto strategy = SoaConversionDataMovementStrategyAttr::DataMovementStrategyType::InSitu;
     if (strategyAttr) {
      strategy = strategyAttr->getDataMovementStrategy();
     }

     if (strategy == SoaConversionDataMovementStrategyAttr::DataMovementStrategyType::InSitu) {
      writeBeforeForStmt(S, prologue);
      writeAfterStmt(S, epilogue);
      return ;
     }

     if (strategy == SoaConversionDataMovementStrategyAttr::DataMovementStrategyType::MoveToOuter) {
      auto parents = Ctx.getParents(*S);
      while (!parents.empty()) {
        auto parent = parents[0];
        auto nodeKindId = parent.getNodeKind().KindId;
        if (nodeKindId != ASTNodeKind::NKI_ForStmt && nodeKindId != ASTNodeKind::NKI_CXXForRangeStmt) {

          // nodes connected directly to method decl (i.e. not nested in any block) are their own parents :)
          if (parent == Ctx.getParents(parent)[0]) break;

          parents = Ctx.getParents(parent);
          continue ;
        }

        auto *outerStmt = parent.get<Stmt>();

        writeBeforeForStmt(outerStmt, prologue);
        writeAfterStmt(outerStmt, epilogue);
        return ;
      }

      llvm::errs() << __FILE__ << ":" << __LINE__ << "No outer ForStmt or CXXForRangeStmt found";
      exit(1);
     }

     if (strategy == SoaConversionDataMovementStrategyAttr::DataMovementStrategyType::MoveToOutermost) {
      auto parents = Ctx.getParents(*S);

      const Stmt *lastStmt = nullptr;
      while (!parents.empty()) {
        auto parent = parents[0];
        auto nodeKindId = parent.getNodeKind().KindId;
        if (nodeKindId != ASTNodeKind::NKI_ForStmt && nodeKindId != ASTNodeKind::NKI_CXXForRangeStmt) {
          parents = Ctx.getParents(parent);
          continue ;
        }

        // nodes connected directly to method decl (i.e. not nested in any block) are their own parents :)
        auto *parentStmt = parent.get<Stmt>();
        if (lastStmt == parentStmt) break;
        lastStmt = parentStmt;
      }

      if (lastStmt == nullptr) {
        llvm::errs() << __FILE__ << ":" << __LINE__ << "No outer ForStmt or CXXForRangeStmt found";
        exit(1);
      }

      writeBeforeForStmt(lastStmt, prologue);
      writeAfterStmt(lastStmt, epilogue);
      return ;
     }

     llvm::errs() << __FILE__ << ":" << __LINE__ << "Unrecognised data movement strategy";
     exit(1);
  }

public:
  SoaConversionASTConsumer(CompilerInstance &CI) : CI(CI), R(CI.getSourceManager().getRewriter()) {}

  bool VisitForStmt(ForStmt *S) {
    const std::vector<const SoaConversionDataItemAttr*> conversionDataAttrs = getAttrs<SoaConversionDataItemAttr>(S);
    const SoaConversionTargetAttr *conversionTargetAttr = getAttr<SoaConversionTargetAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);

    if (conversionDataAttrs.empty() || !conversionTargetAttr || !conversionTargetSizeAttr) return true;

    FunctionDecl *loopParent = getLoopParentFunctionDecl(S);
    if (loopParent->isTemplated()) {
      return true;
    }

    auto *targetDeclRef = getTargetDeclRefExpr(conversionTargetAttr->getTargetRef(), loopParent);
    RecordDecl *targetRecordDecl = getTargetRecordDecl(targetDeclRef, loopParent);
    std::vector<SoaField> soaFields = getFieldsForSoa(targetRecordDecl, conversionDataAttrs);

    ASTContext &Ctx = loopParent->getASTContext();

    std::string soaDef = getSoaDef(conversionTargetAttr->getTargetRef(), soaFields, conversionTargetSizeAttr->getTargetSizeExpr(), Ctx);
    std::string soaConv = getSoaConversionForLoop(conversionTargetAttr->getTargetRef(), targetDeclRef->getType(),
                                                  soaFields, conversionTargetSizeAttr->getTargetSizeExpr());
    std::string prologue = soaDef + "\n" + soaConv + "\n";


    std::string soaUnconv = getSoaUnconversionForLoop(conversionTargetAttr->getTargetRef(), targetDeclRef->getType(),
                                                      soaFields, conversionTargetSizeAttr->getTargetSizeExpr());

    std::string soaFree = getSoaFreeStmts(conversionTargetAttr->getTargetRef(), soaFields);

    std::string epilogue = "\n" + soaUnconv + "\n" + soaFree;

    writePrologueAndEpilogue(S, Ctx, prologue, epilogue);

    MemberExprRewriter fieldAccessRewriter(R);
    ArraySubscriptExprRewriter arraySubscriptExprRewriter(R);
    CXXMemberCallExprRewriter methodCallAccessRewriter(R);

    std::string forLoopIdx = getForStmtIdx(S);
    for (auto field: soaFields) {
      std::string replacement = conversionTargetAttr->getTargetRef().str() + "__SoA__instance" + "." + field.name + "[" + forLoopIdx + "]" ;

      auto *accessDecl = field.inputAccessDecl;
      if (llvm::isa<FieldDecl>(accessDecl) && field.literalIdx == -1) {
          // regular field (not array field)
        fieldAccessRewriter.replaceMemberExprs(llvm::cast<FieldDecl>(accessDecl), targetDeclRef->getDecl()->getNameAsString(), replacement, S);
      } else if (llvm::isa<FieldDecl>(accessDecl) && field.literalIdx >= 0) {
          // array field
          arraySubscriptExprRewriter.replaceArraySubscriptExprs(llvm::cast<FieldDecl>(accessDecl), targetDeclRef->getDecl()->getNameAsString(), replacement, field.literalIdx, S);
      } else if (llvm::isa<CXXMethodDecl>(accessDecl)) {
        methodCallAccessRewriter.replaceMemberExprs(llvm::cast<CXXMethodDecl>(accessDecl), targetDeclRef->getDecl()->getNameAsString(), replacement, S);
      } else {
        llvm::errs() << __FILE__ << __LINE__ << "Access decl is neither a FieldDecl nor a CXXMethodDecl";
        exit(1);
      }

      if (field.inputAccessDecl == field.outputAccessDecl || field.outputAccessDecl == nullptr) {
        // avoid doing the rewrite twice
        continue ;
      }

      accessDecl = field.outputAccessDecl;
      if (llvm::isa<FieldDecl>(accessDecl)) {
        fieldAccessRewriter.replaceMemberExprs(llvm::cast<FieldDecl>(accessDecl), targetDeclRef->getDecl()->getNameAsString(), replacement, S);
      } else if (llvm::isa<CXXMethodDecl>(accessDecl)) {
        methodCallAccessRewriter.replaceMemberExprs(llvm::cast<CXXMethodDecl>(accessDecl), targetDeclRef->getDecl()->getNameAsString(), replacement, S);
      } else {
        llvm::errs() << __FILE__ << __LINE__ << "Access decl is neither a FieldDecl nor a CXXMethodDecl";
        exit(1);
      }

    }
    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *S) {
    const std::vector<const SoaConversionDataItemAttr*> conversionDataAttrs = getAttrs<SoaConversionDataItemAttr>(S);
    const SoaConversionTargetSizeAttr *conversionTargetSizeAttr = getAttr<SoaConversionTargetSizeAttr>(S);

    if (conversionDataAttrs.empty() || !conversionTargetSizeAttr) return true;

    std::string targetRefStr = R.getRewrittenText(S->getRangeInit()->getSourceRange());
    llvm::StringRef targetRef = llvm::StringRef(targetRefStr);

    RecordDecl *targetRecordDecl = getMostLikelyIterableType(S->getLoopVariable()->getType())->getAsRecordDecl();
    std::vector<SoaField> soaFields = getFieldsForSoa(targetRecordDecl, conversionDataAttrs);

    ASTContext &Ctx = targetRecordDecl->getASTContext();

    std::string soaDef = getSoaDef(targetRef, soaFields, conversionTargetSizeAttr->getTargetSizeExpr(), Ctx);
    std::string soaConv = getSoaConversionForRangeLoop(targetRef, S->getLoopVariable()->getType(), soaFields);
    std::string prologue = soaDef + "\n" + soaConv + "\n";

    std::string soaUnconv = getSoaUnconversionForRangeLoop(targetRef, S->getLoopVariable()->getType(), soaFields);
    std::string epilogue = "\n" + soaUnconv;

    writePrologueAndEpilogue(S, Ctx, prologue, epilogue);

    std::string forLoopIdx = targetRefStr + "__main_loop_iter";

    SourceRange newForLoop(S->getSourceRange().getBegin(), S->getBody()->getBeginLoc().getLocWithOffset(-1));
    R.ReplaceText(newForLoop, "for (unsigned int " + forLoopIdx + " = 0; " + forLoopIdx + " < " + conversionTargetSizeAttr->getTargetSizeExpr().str() + "; " + forLoopIdx + "++)");

    MemberExprRewriter fieldAccessRewriter(R);
    CXXMemberCallExprRewriter methodCallAccessRewriter(R);

    for (auto field: soaFields) {
      std::string replacement = targetRefStr + "__SoA__instance" + "." + field.name + "[" + forLoopIdx + "]" ;

      auto *accessDecl = field.inputAccessDecl;
      if (llvm::isa<FieldDecl>(accessDecl)) {
        fieldAccessRewriter.replaceMemberExprs(llvm::cast<FieldDecl>(accessDecl), S->getLoopVariable()->getNameAsString(), replacement, S);
      } else if (llvm::isa<CXXMethodDecl>(accessDecl)) {
        methodCallAccessRewriter.replaceMemberExprs(llvm::cast<CXXMethodDecl>(accessDecl), S->getLoopVariable()->getNameAsString(), replacement, S);
      } else {
        llvm::errs() << __FILE__ << ":" << __LINE__ << "Access decl is neither a FieldDecl nor a CXXMethodDecl";
        exit(1);
      }

      if (field.inputAccessDecl == field.outputAccessDecl || field.outputAccessDecl == nullptr) {
        // avoid doing the rewrite twice
        continue ;
      }

      accessDecl = field.outputAccessDecl;
      if (llvm::isa<FieldDecl>(accessDecl)) {
        fieldAccessRewriter.replaceMemberExprs(llvm::cast<FieldDecl>(accessDecl), S->getLoopVariable()->getNameAsString(), replacement, S);
      } else if (llvm::isa<CXXMethodDecl>(accessDecl)) {
        methodCallAccessRewriter.replaceMemberExprs(llvm::cast<CXXMethodDecl>(accessDecl), S->getLoopVariable()->getNameAsString(), replacement, S);
      } else {
        llvm::errs() << __FILE__ << ":" << __LINE__ << "Access decl is neither a FieldDecl nor a CXXMethodDecl";
        exit(1);
      }

    }
    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
  }

};

#endif // CLANG_SOACONVERSIONASTCONSUMER_H
