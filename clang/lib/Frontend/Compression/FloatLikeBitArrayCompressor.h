//
// Created by p on 16/02/2022.
//

#ifndef CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H
#define CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor.h"


class FloatLikeBitArrayCompressor : public AbstractBitArrayCompressor, public NonIndexedFieldCompressor {
  std::string _structName;
  unsigned int _originalTypeWidth, _headSize, _mantissaSize;

public:

  explicit FloatLikeBitArrayCompressor() {}

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, std::string structName, unsigned int originalTypeWidth, unsigned int headSize, unsigned int mantissaSize)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _structName(structName), _originalTypeWidth(originalTypeWidth), _headSize(headSize), _mantissaSize(mantissaSize) {}

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, FieldDecl *fd)
      : FloatLikeBitArrayCompressor(tableCellSize, tableName, structName, fd->getType(), fd->attrs()) {}

  FloatLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, QualType type, Attrs attrs)
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

  void setOffset(unsigned int offset) override {
    this->_offset = offset;
  }

  std::string getTypeName() override { return _typeStr; }

  unsigned int getCompressedTypeWidth() override {
    return _headSize + _mantissaSize;
  }

  std::string getGetterExpr(std::string thisAccessor) override {
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

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) override {
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

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) override {
    return getSetterExpr(thisAccessor, toBeSetVal) + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    bool isFloatType = type->isFloatingType();
    bool hasCompressAttr = false;
    for( auto *attr : attrs) {
      if (!llvm::isa<CompressTruncateMantissaAttr>(attr)) continue;
      hasCompressAttr = true;
      break;
    }
    return isFloatType && hasCompressAttr;
  }

};

#endif // CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H
