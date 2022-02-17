//
// Created by p on 16/02/2022.
//

#ifndef CLANG_BOOLBITARRAYCOMPRESSOR_H
#define CLANG_BOOLBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor.h"

class BoolBitArrayCompressor : public AbstractBitArrayCompressor, public NonIndexedFieldCompressor {

public:

  explicit BoolBitArrayCompressor() {}

  explicit BoolBitArrayCompressor(unsigned int tableCellSize, std::string tableName)
      : AbstractBitArrayCompressor(tableCellSize, tableName, "bool") {}

  void setOffset(unsigned int offset) override {
    this->_offset = offset;
  }

  unsigned int getCompressedTypeWidth() override {
    return 1;
  }

  std::string getTypeName() override { return _typeStr; }

  std::string getGetterExpr(std::string thisAccessor) override {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "((" + _typeStr + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) override {
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

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) override {
    return getSetterExpr(thisAccessor, toBeSetVal) + ";";
  }

  std::string getTypeCastToOriginalStmt(std::string thisAccessor, std::string retValFieldAccessor) override {
    return retValFieldAccessor + " = " + getGetterExpr(thisAccessor) + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    bool isBoolType = type->isBooleanType();
    bool hasCompressAttr = false;
    for ( auto *attr : attrs) {
      if (!llvm::isa<CompressAttr>(attr)) continue;
      hasCompressAttr = true;
      break;
    }
    return isBoolType && hasCompressAttr;
  }

};

#endif // CLANG_BOOLBITARRAYCOMPRESSOR_H
