//
// Created by p on 16/02/2022.
//

#ifndef CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H
#define CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H

#include "Utils.h"
#include "AbstractBitArrayCompressor2.h"


class FloatLikeBitArrayCompressor : public AbstractBitArrayCompressor2, public NonIndexedFieldCompressor {
  std::string _structName;
  const BuiltinType *originalType;
  unsigned int _mantissaSize;

  unsigned int getOriginalHeadSize() {
    switch (originalType->getKind()) {
    case BuiltinType::Float : return 1 + 8;
    case BuiltinType::Double : return 1 + 11;
    case BuiltinType::LongDouble: return 1 + 15;
    default:
      llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
      exit(1);
    }
  }

  unsigned int getOriginalMantissaSize() {
    switch (originalType->getKind()) {
    case BuiltinType::Float : return 23;
    case BuiltinType::Double : return 52;
    case BuiltinType::LongDouble: return 112;
    default:
      llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
      exit(1);
    }
  }

  std::string getOriginalTypeAsString() {
    switch (originalType->getKind()) {
    case BuiltinType::Float : return "float";
    case BuiltinType::Double : return "double";
    case BuiltinType::LongDouble: return "long double";
    default:
      llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
      exit(1);
    }
  }

  std::string getCorrespondingNumericalTypeAsString() {
    switch (originalType->getKind()) {
    case BuiltinType::Float : return "unsigned int";
    case BuiltinType::Double : return "unsigned long";
    default:
      llvm::errs() << "FloatLikeBitArrayCompressor: Unsupported floating point type for compression\n";
      exit(1);
    }
  }

  unsigned int getCompressedTypeWidthPrivate() {
    return getOriginalHeadSize() + _mantissaSize;
  }

  unsigned int getOriginalTypeWidth() {
    return getOriginalHeadSize() + getOriginalMantissaSize();
  }

public:

  explicit FloatLikeBitArrayCompressor() {}

  FloatLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, FieldDecl *fd)
      : FloatLikeBitArrayCompressor(tableSpec, offset, structName, fd->getType(), fd->attrs()) {}

  FloatLikeBitArrayCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, QualType type, Attrs attrs)
      : AbstractBitArrayCompressor2(tableSpec, {offset, 0}), _structName(structName), originalType(type->getAs<BuiltinType>()) {

    for (auto *attr : attrs) {
      if (!llvm::isa<CompressTruncateMantissaAttr>(attr)) continue;
      _mantissaSize = llvm::cast<CompressTruncateMantissaAttr>(attr)->getMantissaSize();
    }

    this->area = {this->area.offset, this->getCompressedTypeWidthPrivate()};
  }

  std::string getTypeName() override { return this->getOriginalTypeAsString(); }

  unsigned int getCompressedTypeWidth() override {
    return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->fetch();
    std::string truncatedBits = to_constant(this->getOriginalTypeWidth() - getCompressedTypeWidth());
    getterExpr = "((" + this->getCorrespondingNumericalTypeAsString() + ") " + getterExpr + ")";
    getterExpr = "(" + getterExpr + " << " + truncatedBits + ")";
    getterExpr = _structName + "::conv_" + this->getOriginalTypeAsString() + "(" + getterExpr + ").fp";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = "((" + this->getOriginalTypeAsString() + ") " + toBeSetValue + ")";
    toBeSetValue = _structName + "::conv_" + this->getOriginalTypeAsString() + "(" + toBeSetValue + ").i";
    std::string truncatedBits = to_constant(this->getOriginalTypeWidth() - getCompressedTypeWidth());
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
