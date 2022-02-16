//
// Created by p on 16/02/2022.
//

#ifndef CLANG_ENUMBITARRAYCOMPRESSOR_H
#define CLANG_ENUMBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor.h"

class EnumBitArrayCompressor : public AbstractBitArrayCompressor, public NonIndexedFieldCompressor {
  long _valMin, _valMax;
  std::string _intTypeStr;

  long getCompressionConstant() {
    return _valMin;
  }

public:

  explicit EnumBitArrayCompressor() {}

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, long valMin, long valMax, std::string intTypeStr)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _valMin(valMin), _valMax(valMax), _intTypeStr(intTypeStr) {}

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, FieldDecl *fd)
      : EnumBitArrayCompressor(tableCellSize, tableName, fd->getType()) {}

  EnumBitArrayCompressor(unsigned int tableCellSize, std::string tableName, QualType type)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()) {
    _intTypeStr = type->getAs<EnumType>()->getDecl()->getIntegerType().getAsString();

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

  void setOffset(unsigned int offset) override {
    this->_offset = offset;
  }

  std::string getTypeName() override { return _typeStr; }

  unsigned int getCompressedTypeWidth() override {
    unsigned int valueRange = std::abs(_valMax - _valMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

  std::string getGetterExpr(std::string thisAccessor) override {
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

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) override {
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

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) override {
    return getSetterExpr(thisAccessor, toBeSetVal) + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    bool isEnumType = type->isEnumeralType();
    bool hasCompressAttr = false;
    for( auto *attr : attrs) {
      if (!llvm::isa<CompressAttr>(attr)) continue;
      hasCompressAttr = true;
      break;
    }
    return isEnumType && hasCompressAttr;
  }

};


#endif // CLANG_ENUMBITARRAYCOMPRESSOR_H
