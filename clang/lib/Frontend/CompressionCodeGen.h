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

using namespace clang;

class CompressionCodeGen {
  const std::string tableName = "__table";
  const int tableCellSize = 8;
  const std::string tableCellType = "unsigned char";
  RecordDecl *decl;
  CompilerInstance &CI;

  std::string getTypeForTableViewExpr(FieldDecl *fieldDecl) {
    unsigned int tableViewSize = getTableViewWidthForField(fieldDecl);
    if (tableViewSize <= 8) return "unsigned char";
    if (tableViewSize <= 16) return "unsigned short";
    if (tableViewSize <= 32) return "unsigned int";
    if (tableViewSize <= 64) return "unsigned long";
    return "<invalid type>";
  }

  unsigned int getTableViewWidthForField(FieldDecl *fieldDecl) {
      unsigned int bitsMarginLeft = getBitsMarginToLeftTableViewEdge(fieldDecl);
      unsigned int originalSize = getCompressedTypeWidth(fieldDecl);
      unsigned int minWindowSize = bitsMarginLeft + originalSize;
      if (minWindowSize <= 8) return 8;
      if (minWindowSize <= 16) return 16;
      if (minWindowSize <= 32) return 32;
      return 64;
  }

  std::string typeToString(QualType type) {
    if (!type->isBooleanType()) return type.getAsString();
    return "bool";
  }

  unsigned getOriginalTypeWidth(QualType type) {
    if (type->isBooleanType()) return 8;
    if (type->isEnumeralType()) {
      return getOriginalTypeWidth(type->getAs<EnumType>()->getDecl()->getIntegerType());
    }
    if (!type->isIntegerType()) return 64;
    return CI.getASTContext().getIntWidth(type);
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
    return 64;
  }

  unsigned int getEnumTypeWidth(EnumDecl *enumDecl) {
    unsigned int counter = 0;
    for (auto *x : enumDecl->enumerators()) counter++;
    if (counter == 0) return 0;
    unsigned int bitsWidth = ceil(log2(counter));
    return bitsWidth;
  }

  unsigned int getFieldOffset(FieldDecl *fieldDecl) {
    unsigned int offset = 0;
    for (auto *field : decl->fields()) {
      if (field == fieldDecl) break;
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

  std::string getTableViewExpr(FieldDecl *fieldDecl, std::string thisAccessor) {
    int tableIndex = getTableCellStartingIndex(fieldDecl);
    std::string tableViewExpr = "reinterpret_cast<" + getTypeForTableViewExpr(fieldDecl) + "&>(" + thisAccessor + tableName + "[" + std::to_string(tableIndex) + "])";
    return tableViewExpr;
  }

  // 00000___compressed_type_size_as_1s___00000
  unsigned int getAfterFetchMask(FieldDecl *fieldDecl) {
    unsigned int compressedTypeSize = getCompressedTypeWidth(fieldDecl);
    unsigned int leftMargin = getBitsMarginToLeftTableViewEdge(fieldDecl);
    unsigned int mask = (1 << compressedTypeSize) - 1;
    mask = mask << leftMargin;
    return mask;
  }

  // 1111___compressed_type_size_as_0s___111111
  unsigned int getBeforeStoreMask(FieldDecl *fieldDecl) {
    unsigned int rightMargin = getBitsMarginToRightTableViewEdge(fieldDecl);
    unsigned int leftMargin = getBitsMarginToLeftTableViewEdge(fieldDecl);
    unsigned int compressedTypeSize = getCompressedTypeWidth(fieldDecl);
    unsigned int afterFetchMask = getAfterFetchMask(fieldDecl);
    unsigned int tableViewSize = getTableViewWidthForField(fieldDecl);
    unsigned int mask = (1 << tableViewSize) - 1;
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
    return 0;
  }

  unsigned int getTableCellsNeeded() {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      bitCounter += getCompressedTypeWidth(field);
    }
    unsigned int cellsNeeded = ceil(bitCounter / tableCellSize);
    return cellsNeeded;
  }

  std::string getOriginalStructName() {
    return decl->getNameAsString();
  }

  std::string getCompressedStructName() {
      return getOriginalStructName() + "__PACKED";
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
      constructor += getSetterExpr(field, "this->", localVarName + "." + field->getNameAsString()) + "; ";
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

public:

  explicit CompressionCodeGen(RecordDecl *d, CompilerInstance &CI) : decl(d), CI(CI) {}

  std::string getCompressedStructDef() {
    std::string structName = getCompressedStructName();
    std::string tableDefinition = tableCellType + " " + tableName + "[" + std::to_string(getTableCellsNeeded()) + "]";
    std::string emptyConstructor = getEmptyConstructor();
    std::string fromOriginalConstructor = getFromOriginalTypeConstructor();
    std::string typeCastToOriginal = getTypeCastToOriginal();
    std::string structDef = "struct " + structName + " { " + tableDefinition + "; " + emptyConstructor + "; " + fromOriginalConstructor + "; " + typeCastToOriginal + "; };";
    return structDef;
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) {
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

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl)) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask(fieldDecl)) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge(fieldDecl)) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant(fieldDecl));
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};
