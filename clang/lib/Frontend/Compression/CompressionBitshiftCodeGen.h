#ifndef CLANG_COMPRESSIONBITSHIFTCODEGEN_H
#define CLANG_COMPRESSIONBITSHIFTCODEGEN_H

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
#include <cmath>

#include "CompressionICodeGen.h"
#include "DelegatingNonIndexedFieldCompressor.h"
#include "DelegatingFieldCompressor.h"

#include "../SemaIR/SemaIR.h"
#include "../MPI/MpiMappingGenerator.h"

using namespace clang;

static bool isNonIndexAccessCompressionCandidate(FieldDecl *fd) {
  if (DelegatingNonIndexedFieldCompressor().supports(fd)) return true;
  return false;
}

static bool isIndexAccessCompressionCandidate(FieldDecl *fd) {
  if (ConstantSizeArrayBitArrayCompressor().supports(fd)) return true;
  return false;
}

static bool isCompressionCandidate(FieldDecl *fieldDecl) {
  if (DelegatingFieldCompressor().supports(fieldDecl)) return true;
  return false;
}

static bool isCompressionCandidate(RecordDecl *recordDecl) {
  if (!recordDecl) return false; // null record cannot be compressed
  for (auto *field : recordDecl->fields()) {
    if (isCompressionCandidate(field)) return true;
  }
  return false;
}

class CompressionBitshiftCodeGen : public CompressionICodeGen {
  const std::string tableName = "__bitstore";
  unsigned int tableCellSize = 8;
  SemaPrimitiveType tableCellSemaType = SemaPrimitiveType::getForKind(BuiltinType::Kind::Char_U);
  RecordDecl *decl;
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

  unsigned int getTableCellsNeeded() {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      bitCounter += DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), field).getCompressedTypeWidth();
    }
    unsigned int cellsNeeded = ceil(bitCounter / tableCellSize);
    return cellsNeeded;
  }

  unsigned int getFieldOffset(FieldDecl *fd) {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      if (field == fd) break;
      bitCounter += DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), field).getCompressedTypeWidth();
    }
    return bitCounter;
  }

  std::string getOriginalStructName() {
    return decl->getNameAsString();
  }

  std::string getOriginalFullyQualifiedStructName() {
      return decl->getQualifiedNameAsString();
  }

  std::string getEmptyConstructor() {
    std::string constructor = getCompressedStructShortName();
    constructor += "() {";
    for (auto *field : decl->fields()) {
      if (!field->hasInClassInitializer()) continue;
      std::string init = R.getRewrittenText(field->getInClassInitializer()->getSourceRange());
      if (isCompressionCandidate(field)) {
        auto compressor = DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), field);
        compressor.setOffset(getFieldOffset(field));
        constructor += compressor.getCopyConstructorStmt("this->", init) + "\n";
      }
      else {
        constructor += "this->" + field->getNameAsString() + " = " + init + "; ";
      }
    }
    constructor += "}";
    return constructor;
  }

  std::string getFromOriginalTypeConstructor() {
    std::string constructor = getCompressedStructShortName();
    std::string localVarName = "__arg0";
    constructor += " (" + getOriginalFullyQualifiedStructName() + " &&" + localVarName + ") { ";
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        auto compressor = DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), field);
        compressor.setOffset(getFieldOffset(field));
        constructor += compressor.getCopyConstructorStmt("this->", localVarName + "." + field->getNameAsString()) + "\n";
      }
      else {
        constructor += "this->" + field->getNameAsString() + " = " + localVarName + "." + field->getNameAsString() + "; ";
      }
    }
    constructor += "}";
    return constructor;
  }

  std::string getTypeCastToOriginal() {
    std::string typeCast = "operator " + getOriginalFullyQualifiedStructName() + "() { " + getOriginalFullyQualifiedStructName() + " val ;\n";
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        auto compressor = DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), field);
        compressor.setOffset(getFieldOffset(field));
        typeCast += compressor.getTypeCastToOriginalStmt("this->", "val." + field->getNameAsString()) + "\n";
      } else {
        typeCast += "val." + field->getNameAsString() + " = this->" + field->getNameAsString() + ";\n";
      }
    }
    typeCast += "return val;\n";
    typeCast += "}";
    return typeCast;
  }

  std::string getConversionStructs() {
    std::string structs = "";
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      QualType elementType;
      if (field->getType()->isConstantArrayType()) {
        elementType = ConstantSizeArrayBitArrayCompressor::getElementType(llvm::cast<ConstantArrayType>(field->getType()));
      } else {
        elementType = field->getType();
      }
      if (elementType->isFloatingType() && elementType->getAs<BuiltinType>()->getKind() == BuiltinType::Float) {
        structs += "union conv_float { unsigned int i; float fp; conv_float(unsigned int i) { this->i = i; }; conv_float(float f) { this->fp = f; }; }; ";
        break;
      }
    }
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      QualType elementType;
      if (field->getType()->isConstantArrayType()) {
        elementType = ConstantSizeArrayBitArrayCompressor::getElementType(llvm::cast<ConstantArrayType>(field->getType()));
      } else {
        elementType = field->getType();
      }
      if (elementType->isFloatingType() && elementType->getAs<BuiltinType>()->getKind() == BuiltinType::Double) {
        structs += "union conv_double { unsigned long i; double fp; conv_double(unsigned long i) { this->i = i; }; conv_double(double d) { this->fp = d; }; }; ";
        break;
      }
    }
    return structs;
  }

  std::string getConstSizeArrCompressionMethods() {
    std::string methods;
    for (auto *field : decl->fields()) {
      if (!ConstantSizeArrayBitArrayCompressor().supports(field)) continue;
      auto compressor = ConstantSizeArrayBitArrayCompressor(tableCellSize, tableName, getCompressedStructShortName(), field);
      compressor.setOffset(getFieldOffset(field));
      methods += compressor.getGetterMethod() + ";\n";
      methods += compressor.getSetterMethod() + ";\n";
    }
    if (methods.size() > 2) {
      methods.pop_back();
      methods.pop_back();
    }
    return methods;
  }

  static QualType getTypeFromIndirectType(QualType type, std::string &ptrs) {
    while (type->isReferenceType() || type->isAnyPointerType()) {
      if (type->isReferenceType()) {
        type = type.getNonReferenceType();
        ptrs += "&";
      } else if (type->isAnyPointerType()) {
        type = type->getPointeeType();
        ptrs += "*";
      }
    }
    return type;
  }

  std::string convertMethod(CXXMethodDecl *methodDecl) {
    std::string method;

    if (llvm::isa<CXXConstructorDecl>(methodDecl) && methodDecl->getNumParams() == 0) return method; // we already generate a no-args constructor that initialises default fields values

    // we will be changing source in situ in the original class,
    // so we need to restore it later

    // source range from the beginning of the return type (or name in case of constructors)
    // to the end of the args list or specifiers such as noexcept
    SourceRange signatureSourceRange = SourceRange(methodDecl->getSourceRange().getBegin(), methodDecl->getTypeSpecEndLoc());
    std::string oldSignature = R.getRewrittenText(signatureSourceRange);

    for (auto *param : methodDecl->parameters()) {
      std::string ptrs;
      QualType actualType = getTypeFromIndirectType(param->getType(), ptrs);
      if (!actualType->isRecordType() || actualType->getAsRecordDecl() != decl) continue; // constructor arg is not the compressed struct
      R.ReplaceText(param->getTypeSourceInfo()->getTypeLoc().getSourceRange(), getCompressedStructName() + ptrs);
    }

    if (llvm::isa<CXXConstructorDecl>(methodDecl)) {
      // for constructors, we need to change the name of the class
      // to the name of the compressed struct

      // in constructors, type spec starts where arguments start, together with '('
      SourceRange classNameRange = SourceRange(methodDecl->getSourceRange().getBegin(), methodDecl->getTypeSpecStartLoc().getLocWithOffset(-1));
      R.ReplaceText(classNameRange, getCompressedStructName() + " ");
    }

    method = R.getRewrittenText(signatureSourceRange) + ";"; // this just copies over the signature without the impl.

    R.ReplaceText(signatureSourceRange, oldSignature);

    return method;
  }

  std::string getStructMethods() {
    std::string methods;
    if (!llvm::isa<CXXRecordDecl>(decl)) return methods;
    auto *cxxDecl = llvm::cast<CXXRecordDecl>(decl);

    for (auto *method : cxxDecl->methods()) {
      if (method->isImplicit()) continue; // skips autogenerated constructors
      if (method->isDefaulted()) continue; // skips default constructors
      methods += convertMethod(method) + "\n\n";
    }

    return methods;
  }

  std::string getTypedefsForInnerTypes() {
    class ChildRecordOrEnumDeclFinder : public ASTConsumer, public RecursiveASTVisitor<ChildRecordOrEnumDeclFinder> {
    private:
      RecordDecl *parent;
      std::vector<RecordDecl*> records;
      std::vector<EnumDecl*> enums;
    public:
      explicit ChildRecordOrEnumDeclFinder(RecordDecl *parent) : parent(parent) {}

      bool VisitRecordDecl(RecordDecl *decl) {
        if (decl == parent) return true;
        if (decl->getParent() != parent) return true; // we only care about direct children, not grandchildren etc.
        records.push_back(decl);
        return true;
      }

      bool VisitEnumDecl(EnumDecl *decl) {
        if (decl->getParent() != parent) return true; // we only care about direct children, not grandchildren etc.
        enums.push_back(decl);
        return true;
      }

      std::vector<RecordDecl*> getRecords() {
        return records;
      }

      std::vector<EnumDecl*> getEnums() {
        return enums;
      }

    } finder(decl);

    finder.TraverseDecl(decl);

    std::string typedefs;

    for (auto *record : finder.getRecords()) {
      typedefs += "typedef " + decl->getNameAsString() + "::" + record->getNameAsString() + " " + record->getNameAsString() + ";\n";
    }

    for (auto *enumDecl : finder.getEnums()) {
      typedefs += "typedef " + decl->getNameAsString() + "::" + enumDecl->getNameAsString() + " " + enumDecl->getNameAsString() + ";\n";
    }

    return typedefs;
  }

  std::vector<std::unique_ptr<SemaFieldDecl>> getFieldsDecl(SemaRecordDecl &record) {
    std::unique_ptr<SemaPrimitiveType> elementType = std::make_unique<SemaPrimitiveType>();
    elementType->typeKind = tableCellSemaType.typeKind;

    std::unique_ptr<SemaConstSizeArrType> arrType = std::make_unique<SemaConstSizeArrType>();
    arrType->size = getTableCellsNeeded();
    arrType->elementType = std::move(elementType);

    std::unique_ptr<SemaFieldDecl> tableFieldDecl = std::make_unique<SemaFieldDecl>();
    tableFieldDecl->name = tableName;
    tableFieldDecl->type = std::move(arrType);
    tableFieldDecl->parent = &record;

    std::vector<std::unique_ptr<SemaFieldDecl>> fields;

    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) continue;
       std::unique_ptr<SemaFieldDecl> fieldDecl = fromFieldDecl(record, field);
       fields.push_back(std::move(fieldDecl));
    }

    std::sort(fields.begin(), fields.end(), [] (std::unique_ptr<SemaFieldDecl> const& a, std::unique_ptr<SemaFieldDecl> const& b) {
      return a->type->getSize() > b->type->getSize();
    });

    fields.push_back(std::move(tableFieldDecl));

    return fields;
  }

  CXXMethodDecl *getMpiMappingMethodDecl() {
    if (!llvm::isa<CXXRecordDecl>(decl)) return nullptr;
    auto *cxxDecl = llvm::cast<CXXRecordDecl>(decl);
    for (auto *method : cxxDecl->methods()) {
      if (!method->isStatic()) continue;
      for (auto *attr : method->attrs()) {
        if (llvm::isa<MapMpiDatatypeAttr>(attr)) return method;
      }
    }
    return nullptr;
  }

  std::string getNewMpiMappingMethodDecl(CXXMethodDecl *methodDecl) {
    std::string name = methodDecl->getNameAsString();
    return "[[clang::map_mpi_datatype]]\nstatic MPI_Datatype " + name + " ();";
  }

  std::string getCompressedStructShortName() {
    return getOriginalStructName() + "__PACKED";
  }

public:

  explicit CompressionBitshiftCodeGen(RecordDecl *d, ASTContext &Ctx,
                                                      SourceManager &SrcMgr,
                                                      LangOptions &LangOpts,
                                                      Rewriter &R) : decl(d), Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  std::string getCompressedStructName() override {
    return getCompressedStructShortName();
  }

  std::string getFullyQualifiedCompressedStructName() override {
    return getOriginalFullyQualifiedStructName() + "__PACKED";
  }

  std::unique_ptr<SemaRecordDecl> getSemaRecordDecl() override {
    std::string structName = getCompressedStructName();
    std::unique_ptr<SemaRecordDecl> recordDecl = std::make_unique<SemaRecordDecl>();
    recordDecl->name = structName;
    recordDecl->fullyQualifiedName = getFullyQualifiedCompressedStructName();
    recordDecl->fields = getFieldsDecl(*recordDecl);
    return recordDecl;
  }

  std::string getCompressedStructDef() override {
    std::unique_ptr<SemaRecordDecl> recordDecl = getSemaRecordDecl();
    std::string typedefs = getTypedefsForInnerTypes();
    std::string fieldsDecl;
    for (const auto &fieldDecl : recordDecl->fields) {
      fieldsDecl += toSource(*fieldDecl) += ";\n";
    }
    std::string emptyConstructor = getEmptyConstructor();
    std::string fromOriginalConstructor = getFromOriginalTypeConstructor();
    std::string typeCastToOriginal = getTypeCastToOriginal();
    std::string conversionStructs = getConversionStructs();
    std::string constSizeArrCompressionMethods = getConstSizeArrCompressionMethods();
    std::string methods = getStructMethods();

    std::string mpiMapping = "";
    CXXMethodDecl *mpiMappingMethod = getMpiMappingMethodDecl();
    if (mpiMappingMethod) mpiMapping = getNewMpiMappingMethodDecl(mpiMappingMethod);

    std::string structDef = "struct __attribute__((packed)) " + recordDecl->fullyQualifiedName + " {\n"
                            + typedefs + "\n"
                            + fieldsDecl + ";\n"
                            + emptyConstructor + ";\n"
                            + fromOriginalConstructor + ";\n"
                            + typeCastToOriginal + ";\n"
                            + conversionStructs + ";\n"
                            + constSizeArrCompressionMethods + ";\n"
                            + methods + "\n"
                            + mpiMapping + "\n"
                            + "};\n";
    return structDef;
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) override {
    auto delegate = DelegatingNonIndexedFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), fieldDecl);
    delegate.setOffset(getFieldOffset(fieldDecl));
    return delegate.getGetterExpr(thisAccessor);
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) override {
    auto delegate = DelegatingNonIndexedFieldCompressor(tableCellSize, tableName, getCompressedStructShortName(), fieldDecl);
    delegate.setOffset(getFieldOffset(fieldDecl));
    return delegate.getSetterExpr(thisAccessor, toBeSetValue);
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs) override {
    auto delegate = ConstantSizeArrayBitArrayCompressor(tableCellSize, tableName, getCompressedStructShortName(), fieldDecl);
    delegate.setOffset(getFieldOffset(fieldDecl));
    return delegate.getGetterExpr(thisAccessor, idxs);
  }
  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs, std::string toBeSetValue) override {
   auto delegate = ConstantSizeArrayBitArrayCompressor(tableCellSize, tableName, getCompressedStructShortName(), fieldDecl);
   delegate.setOffset(getFieldOffset(fieldDecl));
   return delegate.getSetterExpr(thisAccessor, idxs, toBeSetValue);
  }
};

#endif
