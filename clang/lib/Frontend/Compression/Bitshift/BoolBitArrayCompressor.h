//
// Created by p on 16/02/2022.
//

#ifndef CLANG_BOOLBITARRAYCOMPRESSOR_H
#define CLANG_BOOLBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor2.h"
#include "NonIndexedFieldCompressor.h"

class BoolBitArrayCompressor : public AbstractBitArrayCompressor2, public NonIndexedFieldCompressor {

public:

  explicit BoolBitArrayCompressor() {}

  explicit BoolBitArrayCompressor(TableSpec spec, unsigned int offset)
      : AbstractBitArrayCompressor2(spec, {offset, 1}) {}

  unsigned int getCompressedTypeWidth() override {
    return 1;
  }

  std::string getTypeName() override { return "bool"; }

  std::string getGetterExpr() override {
    std::string getterExpr = this->fetch();
    getterExpr = "((bool) " + getterExpr + ")";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
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
