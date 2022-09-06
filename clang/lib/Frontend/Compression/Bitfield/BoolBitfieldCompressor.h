//
// Created by p on 16/02/2022.
//

#ifndef CLANG_BOOLBITFIELDCOMPRESSOR_H
#define CLANG_BOOLBITFIELDCOMPRESSOR_H

#include "NonIndexedFieldBitfieldCompressor.h"
#include <string>

class BoolBitfieldCompressor : public NonIndexedFieldBitfieldCompressor {

  std::string fieldName, thisAccessor;

public:

  explicit BoolBitfieldCompressor() {}

  explicit BoolBitfieldCompressor(std::string thisAccessor, std::string fieldName) : fieldName(fieldName), thisAccessor(thisAccessor) {}

  unsigned int getCompressedTypeWidth() override {
    return 1;
  }

  std::string getTypeName() override { return "bool"; }

  std::string getGetterExpr() override {
    return this->thisAccessor + this->fieldName;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    return this->thisAccessor + this->fieldName + " = " + toBeSetValue + ";";
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
