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

class ArrayDef {
  QualType elementType;
  std::vector<unsigned int> dimensions;

  void loadArrType(const ConstantArrayType *arrType) {
    while (true) {
      dimensions.push_back(arrType->getSize().getSExtValue());
      if (arrType->getElementType()->isConstantArrayType()) {
        arrType = llvm::cast<ConstantArrayType>(arrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        elementType = arrType->getElementType();
        break;
      }
    }
  }

public:
  explicit ArrayDef(QualType constArrType) {
    if (!constArrType->isConstantArrayType()) {
      llvm::outs() << "ArrayDef constructor: type is not ConstantArrayType\n";
      return;
    }
    auto const *arrType = llvm::cast<ConstantArrayType>(constArrType->getAsArrayTypeUnsafe());
    loadArrType(arrType);
  }

  explicit ArrayDef(const ConstantArrayType *arrType) {
    loadArrType(arrType);
  }

  QualType getElementType() {
    return elementType;
  }

  unsigned int getTotalElements() {
    unsigned int counter = 1;
    for (auto dim : dimensions) counter *= dim;
    return counter;
  }

  unsigned int getLinearItemIndex(std::vector<unsigned int> idxAccessors) {
    if (idxAccessors.size() != dimensions.size()) {
      llvm::errs() << "Accessors idxs len != arr dims len\n";
      return 0;
    }
    unsigned int idx = 0;
    for (unsigned long i = 0; i < idxAccessors.size(); i++) {
      unsigned int multiplier = 1;
      for (unsigned long j = i + 1; j < dimensions.size(); j++) {
        multiplier *= dimensions[j];
      }
      idx += idxAccessors[i] * multiplier;
    }
    return idx;
  }
};

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

  unsigned int getTableViewWidthForField(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
      unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge(fieldDecl, idxs);
      unsigned int originalSize = getCompressedTypeWidth(fieldDecl, idxs);
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
      default: llvm::errs() << "getOriginalTypeWidth: Unsupported floating point type for compression\n";
      }
    }
    if (type->isConstantArrayType()) {
      auto arrayDef = ArrayDef(type);
      return arrayDef.getTotalElements() * getOriginalTypeWidth(arrayDef.getElementType());
    }
    llvm::errs() << "getOriginalTypeWidth: Unsupported type\n";
    return 64;
  }

  unsigned int getCompressedTypeWidth(QualType type, llvm::iterator_range<Decl::attr_iterator> attrs) {
    if (type->isBooleanType()) return 1;
    if (type->isEnumeralType()) {
      return getEnumTypeWidth(const_cast<EnumDecl *>(type->getAs<EnumType>()->getDecl()));
    }
    for (auto *attr : attrs) {
      if (attr->getKind() != clang::attr::CompressRange) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      int minValue = compressRangeAttr->getMinValue();
      int maxValue = compressRangeAttr->getMaxValue();
      unsigned int valueRange = abs(maxValue - minValue + 1);
      unsigned size = ceil(log2(valueRange));
      return size;
    }
    for (auto *attr : attrs) {
      if (!type->isFloatingType()) break;
      if (attr->getKind() != clang::attr::CompressTruncateMantissa) continue;
      auto *compressMantissaAttr = llvm::cast<CompressTruncateMantissaAttr>(attr);
      int mantissaSize = compressMantissaAttr->getMantissaSize();
      int exponentSize;
      switch (type->getAs<BuiltinType>()->getKind()) {
      case BuiltinType::Float:
        exponentSize = 8;
        break;
      case BuiltinType::Double:
        exponentSize = 11;
        break;
      default:
        llvm::errs() << "getCompressedTypeWidth: Unsupported floating point type for compression\n";
        exponentSize = 11;
      }
      return 1 + exponentSize + mantissaSize;
    }
    llvm::errs() << "getCompressedTypeWidth: Unsupported type for compression\n";
    return 64;
  }

  unsigned int getCompressedTypeWidth(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
    auto type = fieldDecl->getType();
    if (!type->isConstantArrayType()) return getCompressedTypeWidth(type, fieldDecl->attrs());
    auto arrDef = ArrayDef(type);
    if (idxs.size() == 0) {
      return arrDef.getTotalElements() * getCompressedTypeWidth(arrDef.getElementType(), fieldDecl->attrs());
    }
    return getCompressedTypeWidth(arrDef.getElementType(), fieldDecl->attrs());
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

  unsigned int getFieldOffset(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
    unsigned int offset = 0;
    for (auto *field : decl->fields()) {
      if (field == fieldDecl) break;
      if (!isCompressionCandidate(field)) continue;
      unsigned int width = getCompressedTypeWidth(field);
      offset += width;
    }
    if (idxs.size() != 0) {
      if (!fieldDecl->getType()->isConstantArrayType()) {
        llvm::outs() << "getFieldOffset: Supplied idxs to a non constant array field\n";
        return offset;
      }
      auto arrDef = ArrayDef(fieldDecl->getType());
      offset += arrDef.getLinearItemIndex(idxs) * getCompressedTypeWidth(arrDef.getElementType(), fieldDecl->attrs());
    }
    return offset;
  }

  unsigned getTableCellStartingIndex(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
    unsigned int offset = getFieldOffset(fieldDecl, idxs);
    return floor(offset / tableCellSize);
  }

  unsigned getBitsMarginToLeftTableViewEdge(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
    unsigned int bitsToLeftEdge = getTableCellStartingIndex(fieldDecl, idxs) * tableCellSize;
    unsigned int offset = getFieldOffset(fieldDecl, idxs);
    unsigned int bitsMarginLeft = offset - bitsToLeftEdge;
    return bitsMarginLeft;
  }

  unsigned int getBitsMarginToRightTableViewEdge(FieldDecl *fieldDecl, std::vector<unsigned int> idxs = std::vector<unsigned int>()) {
    unsigned int tableViewSize = getTableViewWidthForField(fieldDecl, idxs);
    unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge(fieldDecl, idxs);
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
    if (fieldDecl->getType()->isConstantArrayType()) {
      auto type = llvm::cast<ConstantArrayType>(fieldDecl->getType()->getAsArrayTypeUnsafe());
      auto size = type->getSize().getSExtValue();
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

class BoolBitFieldCompressor {

  std::string _fieldName;

public:

  explicit BoolBitFieldCompressor(std::string fieldName) : _fieldName(fieldName) {}

  bool isStoredInBitArray() {
    return false;
  }

  bool isStoredInSeparateField() {
    return true;
  }

  unsigned int getBitArrayStorageSize() {
    llvm::errs() << "getBitArrayStorageSize should not be called\n";
    return 0;
  }

  std::string getFieldDecl() {
    return "bool " + _fieldName + " : 1;";
  }

  std::string getGetterExpr(std::string thisAccessor) {
    return thisAccessor + _fieldName;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string valExpr) {
    return thisAccessor + _fieldName + " = " + valExpr;
  }

  std::string getCopyValueStmt(std::string fromObjAccessor, std::string toObjAccessor) {
    return toObjAccessor + _fieldName + " = " + fromObjAccessor + _fieldName;
  }

};

class AbstractBitArrayCompressor {

public:
  void setOffset(unsigned int offset) {
    this->_offset = offset;
  }

  virtual unsigned int getCompressedTypeWidth() = 0;

protected:

  AbstractBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr)
      : _tableCellSize(tableCellSize), _tableName(tableName), _typeStr(typeStr) {}

  unsigned int _offset, _tableCellSize;
  std::string _tableName, _typeStr;

  std::string getFieldDecl() {
    llvm::errs() << "getFieldDecl should not be called\n";
    return "";
  }

  unsigned int getTableCellStartingIndex() {
    return floor(_offset / _tableCellSize);
  }

  unsigned int getBitsMarginToLeftTableViewEdge() {
    unsigned int bitsToLeftEdge = getTableCellStartingIndex() * _tableCellSize;
    unsigned int bitsMarginLeft = _offset - bitsToLeftEdge;
    return bitsMarginLeft;
  }

  unsigned int getTableViewWidthForField() {
    unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge();
    unsigned int originalSize = getCompressedTypeWidth();
    unsigned int minWindowSize = bitsMarginLeft + originalSize;
    if (minWindowSize <= 8) return 8;
    if (minWindowSize <= 16) return 16;
    if (minWindowSize <= 32) return 32;
    if (minWindowSize <= 64) return 64;
    return 128;
  }

  std::string getIntegerTypeForSize(unsigned int size, bool isConst = true) {
    std::string type = isConst ? "const " : "";
    if (size <= 8) type += "unsigned char";
    else if (size <= 16) type += "unsigned short";
    else if (size <= 32) type += "unsigned int";
    else if (size <= 64) type += "unsigned long";
    else if (size <= 128) type += "unsigned long long";
    else type += "<invalid type>";
    return type;
  }

  std::string getTypeForTableViewExpr(bool isConst = true) {
    unsigned int tableViewSize = getTableViewWidthForField();
    std::string type = getIntegerTypeForSize(tableViewSize, isConst);
    return type;
  }

  std::string getTableViewExpr(std::string thisAccessor, bool isConst = true) {
    int tableIndex = getTableCellStartingIndex();
    std::string tableViewExpr = "reinterpret_cast<" + getTypeForTableViewExpr(isConst) + "&>(" + thisAccessor + _tableName + "[" + std::to_string(tableIndex) + "])";
    return tableViewExpr;
  }

  // 00000___compressed_type_size_as_1s___00000
  unsigned long getAfterFetchMask() {
    unsigned long compressedTypeSize = getCompressedTypeWidth();
    unsigned long leftMargin = getBitsMarginToLeftTableViewEdge();
    unsigned long mask = (1UL << compressedTypeSize) - 1;
    mask = mask << leftMargin;
    return mask;
  }

  // 1111___compressed_type_size_as_0s___111111
  unsigned long getBeforeStoreMask() {
    unsigned long afterFetchMask = getAfterFetchMask();
    unsigned long tableViewSize = getTableViewWidthForField();
    unsigned long mask = (1 << tableViewSize) - 1;
    mask -= afterFetchMask;
    return mask;
  }

};

class BoolBitArrayCompressor : public AbstractBitArrayCompressor {

public:

  explicit BoolBitArrayCompressor(unsigned int tableCellSize, std::string tableName)
      : AbstractBitArrayCompressor(tableCellSize, tableName, "bool") {}

  unsigned int getCompressedTypeWidth() override {
    return 1;
  }

  std::string getGetterExpr(std::string thisAccessor) {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "((" + _typeStr + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    toBeSetValue = "(" + toBeSetValue + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};

class IntLikeBitArrayCompressor : public AbstractBitArrayCompressor {
  long _rangeMin, _rangeMax;

  long getCompressionConstant() {
    return _rangeMin;
  }

public:

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, long rangeMin, long rangeMax)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _rangeMin(rangeMin), _rangeMax(rangeMax) {}

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, FieldDecl *fd)
      : IntLikeBitArrayCompressor(tableCellSize, tableName, fd->getType(), fd->attrs()) {}

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, QualType type, llvm::iterator_range<Decl::attr_iterator> attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()) {
    for (auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      _rangeMin = compressRangeAttr->getMinValue();
      _rangeMax = compressRangeAttr->getMaxValue();
    }
  }

  unsigned int getCompressedTypeWidth() override {
    unsigned int valueRange = std::abs(_rangeMax - _rangeMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

  std::string getGetterExpr(std::string thisAccessor) {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "(" + getter + " + " + compressionConstant + ")";
    getter = "((" + _typeStr + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    toBeSetValue = "(" + toBeSetValue + ")";
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};

class EnumBitArrayCompressor : public AbstractBitArrayCompressor {
  long _valMin, _valMax;
  std::string _intTypeStr;

  long getCompressionConstant() {
    return _valMin;
  }

public:

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, long valMin, long valMax, std::string intTypeStr)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _valMin(valMin), _valMax(valMax), _intTypeStr(intTypeStr) {}

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, FieldDecl *fd)
      : EnumBitArrayCompressor(tableCellSize, tableName, fd->getType()) {}

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, QualType type)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()) {
            _intTypeStr = type->getAs<EnumDecl>()->getIntegerType().getAsString();

            long minValue = LONG_MAX;
            long maxValue = LONG_MIN;
            for (auto *x : type->getAs<EnumType>()->getDecl()->enumerators()) {
              long enumConstantValue = x->getInitVal().getSExtValue();
              if (enumConstantValue < minValue) minValue = enumConstantValue;
              if (enumConstantValue > maxValue) maxValue = enumConstantValue;
            }
            _valMin = minValue;
            _valMax = maxValue;
  }

  unsigned int getCompressedTypeWidth() override {
    unsigned int valueRange = std::abs(_valMax - _valMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

  std::string getGetterExpr(std::string thisAccessor) {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "(" + getter + " + " + compressionConstant + ")";
    getter = "((" + _typeStr + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    toBeSetValue = "(" + toBeSetValue + ")";
    toBeSetValue = "(" + _intTypeStr + ") " + toBeSetValue;
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};

class FloatLikeBitArrayCompressor : public AbstractBitArrayCompressor {
  std::string _structName;
  unsigned int _originalTypeWidth, _headSize, _mantissaSize;

public:

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, std::string structName, unsigned int originalTypeWidth, unsigned int headSize, unsigned int mantissaSize)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _structName(structName), _originalTypeWidth(originalTypeWidth), _headSize(headSize), _mantissaSize(mantissaSize) {}

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, FieldDecl *fd)
      : FloatLikeBitArrayCompressor(tableCellSize, tableName, structName, fd->getType(), fd->attrs()) {}

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, QualType type, llvm::iterator_range<Decl::attr_iterator> attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()), _structName(structName) {

    switch (type->getAs<BuiltinType>()->getKind()) {
    case BuiltinType::Float : _originalTypeWidth = 32; break;
    case BuiltinType::Double : _originalTypeWidth = 64; break;
    default: llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
    }

    switch (type->getAs<BuiltinType>()->getKind()) {
    case BuiltinType::Float : _headSize = 1 + 8; _mantissaSize = 23; break;
    case BuiltinType::Double : _headSize = 1 + 11; _mantissaSize = 52; break;
    default: llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
    }

    for (auto *attr : attrs) {
      if (!llvm::isa<CompressTruncateMantissaAttr>(attr)) continue;
      _mantissaSize = llvm::cast<CompressTruncateMantissaAttr>(attr)->getMantissaSize();
    }
  }

  unsigned int getCompressedTypeWidth() override {
    return _headSize + _mantissaSize;
  }

  std::string getGetterExpr(std::string thisAccessor) {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    std::string intType = getIntegerTypeForSize(_originalTypeWidth, false);
    std::string truncatedBits = std::to_string(_originalTypeWidth - getCompressedTypeWidth()) + "U";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "((" + intType + ") " + getter + ")";
    getter = "(" + getter + " << " + truncatedBits + ")";
    getter = _structName + "::conv_" + _typeStr + "(" + getter + ").fp";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string truncatedBits = std::to_string(_originalTypeWidth - getCompressedTypeWidth()) + "U";
    toBeSetValue = _structName + "::conv_" + _typeStr + "(" + toBeSetValue + ").i";
    toBeSetValue = "(" + toBeSetValue + " >> " + truncatedBits + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};

class ConstantArrayBitArrayCompressor : public AbstractBitArrayCompressor {
  std::vector<unsigned int> _dimensions;
  std::string _fieldName;
  std::string _structName;
  QualType _elementType;
  llvm::iterator_range<Decl::attr_iterator> _attrs;

  unsigned int getTotalElements() {
    unsigned int counter = 1;
    for (auto dim : _dimensions) counter *= dim;
    return counter;
  }

  std::string getLinearItemIndex(std::vector<std::string> idxAccessors) {
    if (idxAccessors.size() != _dimensions.size()) {
      llvm::errs() << "Accessors idxs len != arr dims len\n";
      return 0;
    }
    std::string idx = "0";
    for (unsigned long i = 0; i < idxAccessors.size(); i++) {
      unsigned int multiplier = 1;
      for (unsigned long j = i + 1; j < _dimensions.size(); j++) {
        multiplier *= _dimensions[j];
      }
      idx += " + " + idxAccessors[i] + " * " + std::to_string(multiplier);
    }
    return "(" + idx + ")";
  }

  std::string getElementGetter(std::string thisAccessor, unsigned int elementIndex) {
    if (_elementType->isBooleanType()) {
      auto compressor = BoolBitArrayCompressor(_tableCellSize, _tableName);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string getter = compressor.getGetterExpr(thisAccessor);
      return getter;
    }
    if (_elementType->isIntegerType()) {
      auto compressor = IntLikeBitArrayCompressor(_tableCellSize, _tableName, _elementType, _attrs);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string getter = compressor.getGetterExpr(thisAccessor);
      return getter;
    }
    if (_elementType->isEnumeralType()) {
      auto compressor = EnumBitArrayCompressor(_tableCellSize, _tableName, _elementType);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string getter = compressor.getGetterExpr(thisAccessor);
      return getter;
    }
    if (_elementType->isFloatingType()) {
      auto compressor = FloatLikeBitArrayCompressor(_tableCellSize, _tableName, _structName, _elementType, _attrs);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string getter = compressor.getGetterExpr(thisAccessor);
      return getter;
    }
    llvm::errs() << "ConstantArrayBitArrayCompressor::getElementGetter: Unknown type for compression\n";
    return "";
  }

  std::string getElementSetter(std::string thisAccessor, unsigned int elementIndex, std::string toBeSetValExpr) {
    if (_elementType->isBooleanType()) {
      auto compressor = BoolBitArrayCompressor(_tableCellSize, _tableName);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string setter = compressor.getSetterExpr(thisAccessor, toBeSetValExpr);
      return setter;
    }
    if (_elementType->isIntegerType()) {
      auto compressor = IntLikeBitArrayCompressor(_tableCellSize, _tableName, _elementType, _attrs);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string setter = compressor.getSetterExpr(thisAccessor, toBeSetValExpr);
      return setter;
    }
    if (_elementType->isEnumeralType()) {
      auto compressor = EnumBitArrayCompressor(_tableCellSize, _tableName, _elementType);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string setter = compressor.getSetterExpr(thisAccessor, toBeSetValExpr);
      return setter;
    }
    if (_elementType->isFloatingType()) {
      auto compressor = FloatLikeBitArrayCompressor(_tableCellSize, _tableName, _structName, _elementType, _attrs);
      compressor.setOffset(_offset + elementIndex * compressor.getCompressedTypeWidth());
      std::string setter = compressor.getSetterExpr(thisAccessor, toBeSetValExpr);
      return setter;
    }
    llvm::errs() << "ConstantArrayBitArrayCompressor::getElementGetter: Unknown type for compression\n";
    return "";
  }

public:
  ConstantArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, std::vector<unsigned int> dimensions, std::string fieldName, std::string structName, QualType elementType, llvm::iterator_range<Decl::attr_iterator> attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _dimensions(dimensions), _fieldName(fieldName), _structName(structName), _elementType(elementType), _attrs(attrs) {}

  ConstantArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string fieldName, std::string structName, FieldDecl *fd)
      : ConstantArrayBitArrayCompressor(tableCellSize, tableName, fieldName, structName, fd->getType(), fd->attrs()) {}

  ConstantArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string fieldName, std::string structName, QualType type, llvm::iterator_range<Decl::attr_iterator> attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()), _fieldName(fieldName), _structName(structName), _attrs(attrs) {
    auto *arrType = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
    while (true) {
      _dimensions.push_back(arrType->getSize().getSExtValue());
      if (arrType->getElementType()->isConstantArrayType()) {
        arrType = llvm::cast<ConstantArrayType>(arrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        _elementType = arrType->getElementType();
        break;
      }
    }
  }

  unsigned int getCompressedTypeWidth() override {
    if (_elementType->isBooleanType()) {
      auto compressor = BoolBitArrayCompressor(_tableCellSize, _tableName);
      return compressor.getCompressedTypeWidth();
    }
    if (_elementType->isIntegerType()) {
      auto compressor = IntLikeBitArrayCompressor(_tableCellSize, _tableName, _elementType, _attrs);
      return compressor.getCompressedTypeWidth();
    }
    if (_elementType->isEnumeralType()) {
      auto compressor = EnumBitArrayCompressor(_tableCellSize, _tableName, _elementType);
      return compressor.getCompressedTypeWidth();
    }
    if (_elementType->isFloatingType()) {
      auto compressor = FloatLikeBitArrayCompressor(_tableCellSize, _tableName, _structName, _elementType, _attrs);
      return compressor.getCompressedTypeWidth();
    }
    llvm::errs() << "ConstantArrayBitArrayCompressor::getCompressedTypeWidth: Unknown type for compression\n";
    return 0;
  }

  std::string getGetterMethod() {
    std::vector<std::string> idxs;
    std::string method = _elementType.getAsString() + " get__" + _fieldName + "(";
    for (unsigned int i = 0; i < _dimensions.size(); i++) {
      std::string idx = "idx" + std::to_string(i);
      idxs.push_back(idx);
      method += "unsigned int " + idx + ", ";
    }
    method.pop_back();
    method.pop_back(); // removing the trailing ', '
    method += ") {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      "case " + std::to_string(i) + ": return " + getElementGetter("this->", i) + ";\n";
    }
    method += "default: return (" + _elementType.getAsString() + ") 0;\n";
    method += "}\n"; // close switch
    method += "}\n"; // close method
    return method;
  }

  std::string getSetterMethod() {
    std::vector<std::string> idxs;
    std::string method = "void set__" + _fieldName + "(";
    for (unsigned int i = 0; i < _dimensions.size(); i++) {
      std::string idx = "idx" + std::to_string(i);
      idxs.push_back(idx);
      method += "unsigned int " + idx + ", ";
    }
    method += _elementType.getAsString() + "val";
    method += ") {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      "case " + std::to_string(i) + ": " + getElementSetter("this->", i, "val") + "; break;\n";
    }
    method += "default: break;\n";
    method += "}\n"; // close switch
    method += "}\n"; // close method
    return method;
  }

  std::string getGetterExpr(std::string thisAccessor, std::vector<std::string> idxExprs) {
    std::string methodInvocation = _structName + "::get__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation.pop_back();
    methodInvocation.pop_back(); // remove trailing ', '
    methodInvocation += ")";
    return methodInvocation;
  }

  std::string getSetterExpr(std::string thisAccessor, std::vector<std::string> idxExprs, std::string toBeSetValue) {
    std::string methodInvocation = _structName + "::get__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation += toBeSetValue;
    methodInvocation += ")";
    return methodInvocation;
  }
};

#endif
