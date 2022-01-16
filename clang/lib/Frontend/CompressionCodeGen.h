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

  std::string getTypeForTableViewExpr() {
    switch (tableCellSize) {
      case 8: return "unsigned char";
      case 16: return "unsigned short";
      case 32: return "unsigned int";
      case 64: return "unsigned long";
    }
    return "<invalid type>";
  }

  std::string typeToString(QualType type) {
    if (!type->isBooleanType()) return type.getAsString();
    return "bool";
  }

  int getOriginalTypeWidth(QualType type) {
    if (type->isBooleanType()) return 8;
    if (type->isEnumeralType()) {
      return getOriginalTypeWidth(type->getAs<EnumType>()->getDecl()->getIntegerType());
    }
    if (!type->isIntegerType()) return -999999999;
    return CI.getASTContext().getIntWidth(type);
  }

  int getCompressedTypeWidth(QualType type) {
    if (type->isBooleanType()) return 1;
    if (type->isEnumeralType()) {
      return getEnumTypeWidth(const_cast<EnumDecl *>(type->getAs<EnumType>()->getDecl()));
    }
    if (!type->isIntegerType()) return -999999999;
    return 1;
  }

  int getEnumTypeWidth(EnumDecl *enumDecl) {
    int counter = 0;
    for (auto *x : enumDecl->enumerators()) counter++;
    int bitsWidth = ceil(log2(counter));
    return bitsWidth;
  }

  int getFieldOffset(FieldDecl *fieldDecl) {
    int offset = 0;
    for (auto *field : decl->fields()) {
      if (field == fieldDecl) break;
      auto type = field->getType();
      int width = getCompressedTypeWidth(type);
      offset += width;
    }
    return offset;
  }

  int getTableCellStartingIndex(FieldDecl *fieldDecl) {
    int offset = getFieldOffset(fieldDecl);
    return floor(offset / tableCellSize);
  }

  int getBitsMarginToLeftTableViewEdge(FieldDecl *fieldDecl) {
    int bitsToLeftEdge = getTableCellStartingIndex(fieldDecl) * tableCellSize;
    int offset = getFieldOffset(fieldDecl);
    int bitsMarginLeft = offset - bitsToLeftEdge;
    return bitsMarginLeft;
  }

  int getBitsMarginToRightTableViewEdge(FieldDecl *fieldDecl) {
    int bitsToRightEdge = (getTableCellStartingIndex(fieldDecl) + 1) * tableCellSize;
    int offset = getFieldOffset(fieldDecl);
    int compressedSize = getCompressedTypeWidth(fieldDecl->getType());
    int bitsMarginRight = bitsToRightEdge - offset - compressedSize;
    return bitsMarginRight;
  }

  std::string getTableViewExpr(FieldDecl *fieldDecl, std::string thisAccessor) {
    int tableIndex = getTableCellStartingIndex(fieldDecl);
    std::string tableViewExpr = "reinterpret_cast<" + getTypeForTableViewExpr() + "&>(" + thisAccessor + tableName + "[" + std::to_string(tableIndex) + "])";
    return tableViewExpr;
  }

  // 00000___compressed_type_size_as_1s___00000
  int getAfterFetchMask(FieldDecl *fieldDecl) {
    int compressedTypeSize = getCompressedTypeWidth(fieldDecl->getType());
    int rightMargin = getBitsMarginToRightTableViewEdge(fieldDecl);
    int mask = (1 << compressedTypeSize) - 1;
    mask = mask << rightMargin;
    return mask;
  }

  // 1111___compressed_type_size_as_0s___111111
  int getBeforeStoreMask(FieldDecl *fieldDecl) {
    int rightMargin = getBitsMarginToRightTableViewEdge(fieldDecl);
    int leftMargin = getBitsMarginToLeftTableViewEdge(fieldDecl);
    int compressedTypeSize = getCompressedTypeWidth(fieldDecl->getType());
    int afterFetchMask = getAfterFetchMask(fieldDecl);
    int mask = (1 << tableCellSize) - 1;
    mask -= afterFetchMask;
    return mask;
  }

  int getCompressionConstant(FieldDecl *fieldDecl) {
    return 0;
  }

  int getTableCellsNeeded() {
    double bitCounter = 0;
    for (auto *field : decl->fields()) {
      bitCounter += getCompressedTypeWidth(field->getType());
    }
    int cellsNeeded = ceil(bitCounter / tableCellSize);
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
    std::string afterFetchMask = std::to_string(getAfterFetchMask(fieldDecl));
    std::string bitshiftAgainstRightMargin = std::to_string(getBitsMarginToRightTableViewEdge(fieldDecl));
    std::string compressionConstant = std::to_string(getCompressionConstant(fieldDecl));
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstRightMargin + ")";
    getter = "(" + getter + " + " + compressionConstant + ")";
    getter = "((" + typeToString(fieldDecl->getType()) + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) {
    std::string tableView = getTableViewExpr(fieldDecl, thisAccessor);
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask(fieldDecl));
    std::string bitshiftAgainstRightMargin = std::to_string(getBitsMarginToRightTableViewEdge(fieldDecl));
    std::string compressionConstant = std::to_string(getCompressionConstant(fieldDecl));
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstRightMargin + ")";
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

};
