//
// Created by p on 16/02/2022.
//

#ifndef CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H
#define CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor2.h"


class FloatLikeBitArrayCompressor : public AbstractBitArrayCompressor2, public NonIndexedFieldCompressor {
  std::string _structName;
  std::string typeStr;
  unsigned int _originalTypeWidth, _headSize, _mantissaSize;

  unsigned int getCompressedTypeWidthPrivate() {
    return _headSize + _mantissaSize;
  }

public:

  explicit FloatLikeBitArrayCompressor() {}

  FloatLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, FieldDecl *fd)
      : FloatLikeBitArrayCompressor(tableSpec, offset, structName, fd->getType(), fd->attrs()) {}

  FloatLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, QualType type, Attrs attrs)
      : AbstractBitArrayCompressor2(tableSpec, {offset, 0}), _structName(structName), typeStr(type.getAsString()) {

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

    this->area = {this->area.offset, this->getCompressedTypeWidthPrivate()};
  }

  std::string getTypeName() override { return this->typeStr; }

  unsigned int getCompressedTypeWidth() override {
    return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->fetch();
    std::string truncatedBits = std::to_string(_originalTypeWidth - getCompressedTypeWidth()) + "U";
    getterExpr = "(" + getterExpr + " << " + truncatedBits + ")";
    getterExpr = _structName + "::conv_" + this->typeStr + "(" + getterExpr + ").fp";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = _structName + "::conv_" + this->typeStr + "(" + toBeSetValue + ").i";
    std::string truncatedBits = std::to_string(_originalTypeWidth - getCompressedTypeWidth()) + "U";
    toBeSetValue = "(" + toBeSetValue + " >> " + truncatedBits + ")";
    std::string setterStmt = this->store(toBeSetValue);
    return setterStmt;
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
