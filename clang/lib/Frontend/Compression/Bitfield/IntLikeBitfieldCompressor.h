//
// Created by p on 16/02/2022.
//

#ifndef CLANG_INTLIKEBITFIELDCOMPRESSOR_H
#define CLANG_INTLIKEBITFIELDCOMPRESSOR_H

#include "Utils.h"

class IntLikeBitfieldCompressor : public NonIndexedFieldBitfieldCompressor {
  long _rangeMin, _rangeMax;

  std::string thisAccessor;
  std::string fieldName;
  std::string typeStr;

  unsigned int getCompressedTypeWidthPrivate() {
    unsigned int valueRange = std::abs(_rangeMax - _rangeMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

public:

  explicit IntLikeBitfieldCompressor() {}

  IntLikeBitfieldCompressor(std::string thisAccessor, FieldDecl *fd)
      : IntLikeBitfieldCompressor(thisAccessor, fd->getNameAsString(), fd->getType(), fd->attrs()) {}

  IntLikeBitfieldCompressor(std::string thisAccessor, std::string fieldName, QualType type, Attrs attrs)
      : thisAccessor(thisAccessor), fieldName(fieldName), typeStr(type.getAsString()) {
    for (auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      _rangeMin = compressRangeAttr->getMinValue();
      _rangeMax = compressRangeAttr->getMaxValue();
    }
  }

  std::string getTypeName() override { return this->typeStr; }

  unsigned int getCompressedTypeWidth() override {
      return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->thisAccessor + this->fieldName;
    getterExpr = "((" + this->typeStr + ") " + getterExpr + ")";
    getterExpr = "(" + getterExpr + " + " + to_constant(this->_rangeMin) + ")";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = "((" + toBeSetValue + ") - " + to_constant(this->_rangeMin) + ")";
    std::string setterExpr = this->thisAccessor + this->fieldName + " = " + toBeSetValue + ";";
    return setterExpr;
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) override {
    return getSetterExpr(toBeSetVal);
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) override {
    return retValFieldAccessor + " = " + getGetterExpr() + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getNameAsString(), d->getType(), d->attrs());
  }

  bool supports(std::string fieldName, QualType type, Attrs attrs) override {
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
