//
// Created by p on 16/02/2022.
//

#ifndef CLANG_ENUMBITFIELDCOMPRESSOR_H
#define CLANG_ENUMBITFIELDCOMPRESSOR_H

#include "Utils.h"

class EnumBitfieldCompressor : public NonIndexedFieldBitfieldCompressor {
  long _valMin, _valMax;
  std::string thisAccessor, fieldName;
  std::string typeStr;
  std::string _intTypeStr;

  unsigned int getCompressedTypeWidthPrivate() {
    unsigned int valueRange = std::abs(_valMax - _valMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

public:

  explicit EnumBitfieldCompressor() {}

  EnumBitfieldCompressor(std::string thisAccessor, FieldDecl *fd)
      : EnumBitfieldCompressor(thisAccessor, fd->getNameAsString(), fd->getType()) {}

  EnumBitfieldCompressor(std::string thisAccessor, std::string fieldName, QualType type)
      : thisAccessor(thisAccessor), fieldName(fieldName), typeStr(type.getAsString()) {
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

  std::string getTypeName() override { return this->typeStr; }

  unsigned int getCompressedTypeWidth() override {
    return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->thisAccessor + this->fieldName;
    getterExpr = "(" + getterExpr + " + " + to_constant(this->_valMin) + ")";
    getterExpr = "((" + this->typeStr + ") " + getterExpr + ")";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = "((" + this->_intTypeStr + ") " + toBeSetValue + ")";
    toBeSetValue = "((" + toBeSetValue + ") - " + to_constant(this->_valMin) + ")";
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
