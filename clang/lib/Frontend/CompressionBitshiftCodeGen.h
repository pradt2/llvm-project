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

using namespace clang;

static bool isCompressionCandidate(FieldDecl *fieldDecl) {
  for (auto *fieldAttr : fieldDecl->attrs()) {
    switch (fieldAttr->getKind()) {
    case clang::attr::Compress:
    case clang::attr::CompressRange:
    case clang::attr::CompressTruncateMantissa:
      return true;
    default:
      break;
    }
  }
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
  const int tableCellSize = 8;
  const std::string tableCellType = "unsigned char";
  RecordDecl *decl;
  CompilerInstance &CI;

  std::string getIntegerTypeForSize(int size, bool isConst = true) {
    std::string type = isConst ? "const " : "";
    if (size <= 8) type += "unsigned char";
    else if (size <= 16) type += "unsigned short";
    else if (size <= 32) type += "unsigned int";
    else if (size <= 64) type += "unsigned long";
    else if (size <= 128) type += "unsigned long long";
    else type += "<invalid type>";
    return type;
  }

  std::string getTypeForTableViewExpr(FieldDecl *fieldDecl, bool isConst = true) {
    unsigned int tableViewSize = getTableViewWidthForField(fieldDecl);
    std::string type = getIntegerTypeForSize(tableViewSize, isConst);
    return type;
  }

  unsigned int getTableViewWidthForField(FieldDecl *fieldDecl) {
      unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge(fieldDecl);
      unsigned int originalSize = getCompressedTypeWidth(fieldDecl);
      unsigned int minWindowSize = bitsMarginLeft + originalSize;
      if (minWindowSize <= 8) return 8;
      if (minWindowSize <= 16) return 16;
      if (minWindowSize <= 32) return 32;
      if (minWindowSize <= 64) return 64;
      return 128;
  }

  std::string typeToString(QualType type) {
    if (!type->isBooleanType()) return type.getAsString();
    return "bool";
  }

  unsigned int getOriginalTypeWidth(QualType type) {
    if (type->isBooleanType()) return 8;
    if (type->isEnumeralType()) {
      return getOriginalTypeWidth(type->getAs<EnumType>()->getDecl()->getIntegerType());
    }
    if (type->isIntegerType()) return CI.getASTContext().getIntWidth(type);
    if (type->isFloatingType()) {
      switch (type->getAs<BuiltinType>()->getKind()) {
      case BuiltinType::Float : return 32;
      case BuiltinType::Double : return 64;
      default: llvm::outs() << "Unsupported floating point type for compression\n";
      }
    }
    return 64;
  }

  unsigned int getCompressedTypeWidth(FieldDecl *fieldDecl) {
    auto type = fieldDecl->getType();
    if (type->isBooleanType()) return 1;
    if (type->isEnumeralType()) {
      return getEnumTypeWidth(const_cast<EnumDecl *>(type->getAs<EnumType>()->getDecl()));
    }
    for (auto *attr : fieldDecl->attrs()) {
      if (attr->getKind() != clang::attr::CompressRange) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      int minValue = compressRangeAttr->getMinValue();
      int maxValue = compressRangeAttr->getMaxValue();
      unsigned int valueRange = abs(maxValue - minValue);
      if (valueRange == 1) return 1;
      unsigned size = ceil(log2(valueRange));
      return size;
    }
    for (auto *attr : fieldDecl->attrs()) {
      if (!fieldDecl->getType()->isFloatingType()) break;
      if (attr->getKind() != clang::attr::CompressTruncateMantissa) continue;
      auto *compressMantissaAttr = llvm::cast<CompressTruncateMantissaAttr>(attr);
      int mantissaSize = compressMantissaAttr->getMantissaSize();
      int exponentSize;
      switch (fieldDecl->getType()->getAs<BuiltinType>()->getKind()) {
      case BuiltinType::Float:
        exponentSize = 8;
        break;
      case BuiltinType::Double:
        exponentSize = 11;
        break;
      default:
        llvm::errs() << "Unsupported floating point type for compression\n";
        exponentSize = 11;
      }
      return 1 + exponentSize + mantissaSize;
    }
    return 64;
  }

  unsigned int getEnumTypeWidth(EnumDecl *enumDecl) {
    long minValue = LONG_MAX;
    long maxValue = LONG_MIN;
    for (auto *x : enumDecl->enumerators()) {
      long enumConstantValue = x->getInitVal().getSExtValue();
      if (enumConstantValue < minValue) minValue = enumConstantValue;
      if (enumConstantValue > maxValue) maxValue = enumConstantValue;
    }
    unsigned int bitsWidth = ceil(log2(maxValue - minValue + 1));
    return bitsWidth;
  }

  unsigned int getFieldOffset(FieldDecl *fieldDecl) {
    unsigned int offset = 0;
    for (auto *field : decl->fields()) {
      if (field == fieldDecl) break;
      if (!isCompressionCandidate(field)) continue;
      unsigned int width = getCompressedTypeWidth(field);
      offset += width;
    }
    return offset;
  }

  unsigned getTableCellStartingIndex(FieldDecl *fieldDecl) {
    unsigned int offset = getFieldOffset(fieldDecl);
    return floor(offset / tableCellSize);
  }

  unsigned getBitsMarginToLeftTableViewEdge(FieldDecl *fieldDecl) {
    unsigned int bitsToLeftEdge = getTableCellStartingIndex(fieldDecl) * tableCellSize;
    unsigned int offset = getFieldOffset(fieldDecl);
    unsigned int bitsMarginLeft = offset - bitsToLeftEdge;
    return bitsMarginLeft;
  }

  unsigned int getBitsMarginToRightTableViewEdge(FieldDecl *fieldDecl) {
    unsigned int tableViewSize = getTableViewWidthForField(fieldDecl);
    unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge(fieldDecl);
    unsigned int compressedSize = getCompressedTypeWidth(fieldDecl);
    unsigned bitsMarginRight = tableViewSize - bitsMarginLeft - compressedSize;
    return bitsMarginRight;
  }

  std::string getTableViewExpr(FieldDecl *fieldDecl, std::string thisAccessor, bool isConst = true) {
    int tableIndex = getTableCellStartingIndex(fieldDecl);
    std::string tableViewExpr = "reinterpret_cast<" + getTypeForTableViewExpr(fieldDecl, isConst) + "&>(" + thisAccessor + tableName + "[" + std::to_string(tableIndex) + "])";
    return tableViewExpr;
  }

  // 00000___compressed_type_size_as_1s___00000
  unsigned long getAfterFetchMask(FieldDecl *fieldDecl) {
    unsigned long compressedTypeSize = getCompressedTypeWidth(fieldDecl);
    unsigned long leftMargin = getBitsMarginToLeftTableViewEdge(fieldDecl);
    unsigned long mask = (1UL << compressedTypeSize) - 1;
    mask = mask << leftMargin;
    return mask;
  }

  // 1111___compressed_type_size_as_0s___111111
  unsigned long getBeforeStoreMask(FieldDecl *fieldDecl) {
    unsigned long afterFetchMask = getAfterFetchMask(fieldDecl);
    unsigned long tableViewSize = getTableViewWidthForField(fieldDecl);
    unsigned long mask = (1 << tableViewSize) - 1;
    mask -= afterFetchMask;
    return mask;
  }

  int getCompressionConstant(FieldDecl *fieldDecl) {
    for (auto *attr : fieldDecl->attrs()) {
      if (attr->getKind() != clang::attr::CompressRange) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      int minValue = compressRangeAttr->getMinValue();
      return minValue;
    }
    if (!fieldDecl->getType()->isEnumeralType()) return 0;
    long minValue = LONG_MAX;
    for (auto *x : fieldDecl->getType()->getAs<EnumType>()->getDecl()->enumerators()) {
      long enumConstantValue = x->getInitVal().getSExtValue();
      if (enumConstantValue < minValue) minValue = enumConstantValue;
    }
    return minValue == LONG_MAX ? 0 : minValue;
  }

  unsigned int getTableCellsNeeded() {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      if (!isCompressionCandidate(field)) continue;
      bitCounter += getCompressedTypeWidth(field);
    }
    unsigned int cellsNeeded = ceil(bitCounter / tableCellSize);
    return cellsNeeded;
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
        constructor += getSetterExpr(field, "this->", localVarName + "." + field->getNameAsString()) + "; ";
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
      typeCast += getGetterExpr(field, "this->") + ", ";
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


  std::string getGetterExprIntLikeType(FieldDecl *fieldDecl, std::string thisAccessor) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl)) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge(fieldDecl)) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant(fieldDecl));
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "(" + getter + " + " + compressionConstant + ")";
    getter = "((" + typeToString(fieldDecl->getType()) + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExprIntLikeType(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl)) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask(fieldDecl)) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge(fieldDecl)) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant(fieldDecl));
    toBeSetValue = "(" + toBeSetValue + ")";
    if (fieldDecl->getType()->isEnumeralType()) {
      std::string intTypeStr = fieldDecl->getType()->getAs<EnumType>()->getDecl()->getIntegerType().getAsString();
      toBeSetValue = "(" + intTypeStr + ") " + toBeSetValue;
    }
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

  std::string getGetterExprFloatLikeType(FieldDecl *fieldDecl, std::string thisAccessor) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl)) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge(fieldDecl)) + "U";
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    std::string originalTypeName = typeToString(fieldDecl->getType());
    std::string intType = getIntegerTypeForSize(getOriginalTypeWidth(fieldDecl->getType()), false);
    std::string truncatedBits = std::to_string(getOriginalTypeWidth(fieldDecl->getType()) - getCompressedTypeWidth(fieldDecl)) + "U";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "((" + intType + ") " + getter + ")";
    getter = "(" + getter + " << " + truncatedBits + ")";
    getter = getCompressedStructName() + "::conv_" + originalTypeName + "(" + getter + ").fp";
    return getter;
  }

  std::string getSetterExprFloatLikeType(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl)) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask(fieldDecl)) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge(fieldDecl)) + "U";
    std::string originalTypeName = typeToString(fieldDecl->getType());
    std::string truncatedBits = std::to_string(getOriginalTypeWidth(fieldDecl->getType()) - getCompressedTypeWidth(fieldDecl)) + "U";
    toBeSetValue = getCompressedStructName() + "::conv_" + originalTypeName + "(" + toBeSetValue + ").i";
    toBeSetValue = "(" + toBeSetValue + " >> " + truncatedBits + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
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
    if (fieldDecl->getType()->isIntegerType() || fieldDecl->getType()->isEnumeralType()) {
      return this->getGetterExprIntLikeType(fieldDecl, thisAccessor);
    }
    if (fieldDecl->getType()->isFloatingType()) {
      return this->getGetterExprFloatLikeType(fieldDecl, thisAccessor);
    }
    llvm::errs() << "Unsupported type for compression\n";
    return "<cannot generate getter expr>";
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) override {
    if (fieldDecl->getType()->isFixedPointOrIntegerType() || fieldDecl->getType()->isEnumeralType()) {
      return this->getSetterExprIntLikeType(fieldDecl, thisAccessor, toBeSetValue);
    }
    if (fieldDecl->getType()->isFloatingType()) {
      return this->getSetterExprFloatLikeType(fieldDecl, thisAccessor, toBeSetValue);
    }
    llvm::errs() << "Unsupported type for compression\n";
    return "<cannot generate setter expr>";
  }

};

#endif
