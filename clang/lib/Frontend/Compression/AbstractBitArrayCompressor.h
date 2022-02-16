//
// Created by p on 16/02/2022.
//

#ifndef CLANG_ABSTRACTBITARRAYCOMPRESSOR_H
#define CLANG_ABSTRACTBITARRAYCOMPRESSOR_H

#include "NonIndexedFieldCompressor.h"

class AbstractBitArrayCompressor {

public:

  virtual unsigned int getCompressedTypeWidth() = 0;

protected:

  explicit AbstractBitArrayCompressor() {}

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

#endif // CLANG_ABSTRACTBITARRAYCOMPRESSOR_H
