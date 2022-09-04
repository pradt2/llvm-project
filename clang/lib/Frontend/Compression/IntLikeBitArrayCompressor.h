//
// Created by p on 16/02/2022.
//

#ifndef CLANG_INTLIKEBITARRAYCOMPRESSOR_H
#define CLANG_INTLIKEBITARRAYCOMPRESSOR_H

#include "Utils.h"

class IntLikeBitArrayCompressor : public AbstractBitArrayCompressor2, public NonIndexedFieldCompressor {
  long _rangeMin, _rangeMax;

  std::string typeStr;

  unsigned int getCompressedTypeWidthPrivate() {
    unsigned int valueRange = std::abs(_rangeMax - _rangeMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

public:

  explicit IntLikeBitArrayCompressor() {}

  IntLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, FieldDecl *fd)
      : IntLikeBitArrayCompressor(tableSpec, offset, fd->getType(), fd->attrs()) {}

  IntLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, QualType type, Attrs attrs)
      : AbstractBitArrayCompressor2(tableSpec, {offset, 0}), typeStr(type.getAsString()) {
    for (auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      _rangeMin = compressRangeAttr->getMinValue();
      _rangeMax = compressRangeAttr->getMaxValue();
    }
    this->area = {this->area.offset, this->getCompressedTypeWidthPrivate()};
  }

  std::string getTypeName() override { return this->typeStr; }

  unsigned int getCompressedTypeWidth() override {
      return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->fetch();
    getterExpr = "(" + getterExpr + " + " + to_constant(this->_rangeMin) + ")";
    getterExpr = "((" + this->typeStr + ") " + getterExpr + ")";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = "((" + toBeSetValue + ") - " + to_constant(this->_rangeMin) + ")";
    std::string setterExpr = this->store(toBeSetValue);
    return setterExpr;
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) override {
    return getSetterExpr(toBeSetVal);
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) override {
    return retValFieldAccessor + " = " + getGetterExpr() + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    bool isIntegerType = type->isIntegerType();
    bool hasCompressAttr = false;
    for( auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      hasCompressAttr = true;
      break;
    }
    return isIntegerType && hasCompressAttr;
  }

};

#endif // CLANG_INTLIKEBITARRAYCOMPRESSOR_H
