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

#include "./CompressionICodeGen.h"
#include "./Compression/DelegatingNonIndexedFieldCompressor.h"
#include "./Compression/DelegatingFieldCompressor.h"

using namespace clang;

static bool isCompressionCandidate(FieldDecl *fieldDecl) {
  if (DelegatingFieldCompressor().supports(fieldDecl)) return true;
  return false;
}

static bool isCompressionCandidate(RecordDecl *recordDecl) {
  for (auto *field : recordDecl->fields()) {
    if (isCompressionCandidate(field)) return true;
  }
  return false;
}

class CompressionBitshiftCodeGen : public CompressionICodeGen {
  const std::string tableName = "__table";
  unsigned int tableCellSize = 8;
  const std::string tableCellType = "unsigned char";
  RecordDecl *decl;
  CompilerInstance &CI;

  unsigned int getTableCellsNeeded() {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      bitCounter += DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructName(), field).getCompressedTypeWidth();
    }
    unsigned int cellsNeeded = ceil(bitCounter / tableCellSize);
    return cellsNeeded;
  }

  unsigned int getFieldOffset(FieldDecl *fd) {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      if (field == fd) break;
      bitCounter += DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructName(), field).getCompressedTypeWidth();
    }
    return bitCounter;
  }

  std::string getOriginalStructName() {
    return decl->getNameAsString();
  }

  std::string getEmptyConstructor() {
    std::string constructor = getCompressedStructName();
    constructor += "() {}";
    return constructor;
  }

  std::string getFromOriginalTypeConstructor() {
    std::string constructor = getCompressedStructName();
    std::string localVarName = "__arg0";
    constructor += " (" + getOriginalStructName() + " &&" + localVarName + ") { ";
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        constructor += DelegatingFieldCompressor(tableCellSize, tableName, getCompressedStructName(), field).getCopyConstructorStmt("this->", localVarName + "." + field->getNameAsString());
//        constructor += getSetterExpr(field, "this->", localVarName + "." + field->getNameAsString()) + "; ";
      }
      else {
        constructor += "this->" + field->getNameAsString() + " = " + localVarName + "." + field->getNameAsString() + "; ";
      }
    }
    constructor += "} ";
    return constructor;
  }

  std::string getTypeCastToOriginal() {
    std::string typeCast = "operator " + getOriginalStructName() + "() { return { ";
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        typeCast += getGetterExpr(field, "this->") + ", ";
      } else {
        typeCast += "this->" + field->getNameAsString() + ", ";
      }
    }
    typeCast += " }; }";
    return typeCast;
  }

  std::string getConversionStructs() {
    std::string structs = "";
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      if (!field->getType()->isFloatingType()) continue;
      if (field->getType()->getAs<BuiltinType>()->getKind() != BuiltinType::Float) continue;
      structs += "union conv_float { unsigned int i; float fp; conv_float(unsigned int i) { this->i = i; }; conv_float(float f) { this->fp = f; }; }; ";
      break;
    }
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      if (!field->getType()->isFloatingType()) continue;
      if (field->getType()->getAs<BuiltinType>()->getKind() != BuiltinType::Double) continue;
      structs += "union conv_double { unsigned long i; double fp; conv_double(unsigned long i) { this->i = i; }; conv_double(double d) { this->fp = d; }; }; ";
      break;
    }
    return structs;
  }

  std::string getFieldsDecl() {
    std::string tableDefinition = tableCellType + " " + tableName + "[" + std::to_string(getTableCellsNeeded()) + "]; ";
    std::string nonCompressedFields;
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) continue;
       nonCompressedFields += CI.getSourceManager().getRewriter()->getRewrittenText(field->getSourceRange()) + "; ";
    }
    if (nonCompressedFields.length() > 2) {
      nonCompressedFields.pop_back();
      nonCompressedFields.pop_back();
    }
    return tableDefinition + nonCompressedFields;
  }

public:

  explicit CompressionBitshiftCodeGen(RecordDecl *d, CompilerInstance &CI) : decl(d), CI(CI) {}

  std::string getCompressedStructName() override {
    return getOriginalStructName() + "__COMPRESSED";
  }

  std::string getCompressedStructDef() override {
    std::string structName = getCompressedStructName();
    std::string fieldsDecl = getFieldsDecl();
    std::string emptyConstructor = getEmptyConstructor();
    std::string fromOriginalConstructor = getFromOriginalTypeConstructor();
    std::string typeCastToOriginal = getTypeCastToOriginal();
    std::string conversionStructs = getConversionStructs();
    std::string structDef = std::string("\n#pragma pack(push, 1)\n")
                            + "struct " + structName + " {\n"
                            + fieldsDecl + ";\n"
                            + emptyConstructor + ";\n"
                            + fromOriginalConstructor + ";\n"
                            + typeCastToOriginal + ";\n"
                            + conversionStructs + ";\n"
                            + "};\n"
                            + "#pragma pack(pop)\n";
    return structDef;
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) override {
    auto delegate = DelegatingNonIndexedFieldCompressor(tableCellSize, tableName, getCompressedStructName(), fieldDecl);
    delegate.setOffset(getFieldOffset(fieldDecl));
    return delegate.getGetterExpr(thisAccessor);
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) override {
    auto delegate = DelegatingNonIndexedFieldCompressor(tableCellSize, tableName, getCompressedStructName(), fieldDecl);
    delegate.setOffset(getFieldOffset(fieldDecl));
    return delegate.getSetterExpr(thisAccessor, toBeSetValue);
  }

};

#endif
