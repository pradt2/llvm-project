//
// Created by p on 16/02/2022.
//

#ifndef CLANG_FLOATLIKEBITFIELDCOMPRESSOR_H
#define CLANG_FLOATLIKEBITFIELDCOMPRESSOR_H

#include "Utils.h"

class FloatLikeBitfieldCompressor : public NonIndexedFieldBitfieldCompressor {
  std::string _structName;
  std::string thisAccessor, fieldName;

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

  explicit FloatLikeBitfieldCompressor() {}

  FloatLikeBitfieldCompressor(std::string structName, std::string thisAccessor, FieldDecl *fd)
      : FloatLikeBitfieldCompressor(structName, thisAccessor, fd->getNameAsString(), fd->getType(), fd->attrs()) {}

  FloatLikeBitfieldCompressor(std::string structName, std::string thisAccessor, std::string fieldName, QualType type, Attrs attrs)
      : _structName(structName), thisAccessor(thisAccessor), fieldName(fieldName), originalType(type->getAs<BuiltinType>()) {

    for (auto *attr : attrs) {
      if (!llvm::isa<CompressTruncateMantissaAttr>(attr)) continue;
      _mantissaSize = llvm::cast<CompressTruncateMantissaAttr>(attr)->getMantissaSize();
        return;
    }

    if (fieldName.find("__truncate_mantissa_") == std::string::npos) return;

    auto last_idx = fieldName.find_last_of('_');
    auto mantissaBits = std::stoi(fieldName.substr(last_idx + 1));
    _mantissaSize = mantissaBits;
  }

  std::string getTypeName() override { return this->getOriginalTypeAsString(); }

  unsigned int getCompressedTypeWidth() override {
    return this->getCompressedTypeWidthPrivate();
  }

  std::string getGetterExpr() override {
    std::string getterExpr = this->thisAccessor + this->fieldName;
    std::string truncatedBits = to_constant(this->getOriginalTypeWidth() - getCompressedTypeWidth());
    getterExpr = "((" + this->getCorrespondingNumericalTypeAsString() + ") " + getterExpr + ")";
    getterExpr = "(" + getterExpr + " << " + truncatedBits + ")";

    std::string methodName = "__internal_";
    if (this->getOriginalTypeAsString() == "float") methodName += "itof";
    else if (this->getOriginalTypeAsString() == "double") methodName += "ltod";

    getterExpr = methodName + "(" + getterExpr + ")";
    return getterExpr;
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    toBeSetValue = "((" + this->getOriginalTypeAsString() + ") " + toBeSetValue + ")";

      std::string methodName = "__internal_";
      if (this->getOriginalTypeAsString() == "float") methodName += "ftoi";
      else if (this->getOriginalTypeAsString() == "double") methodName += "dtol";

    toBeSetValue = methodName + "(" + toBeSetValue + ")";
    std::string truncatedBits = to_constant(this->getOriginalTypeWidth() - getCompressedTypeWidth());
    toBeSetValue = "(" + toBeSetValue + " >> " + truncatedBits + ")";
    std::string setterStmt = this->thisAccessor + this->fieldName + " = " + toBeSetValue + ";";
    return setterStmt;
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) override {
    return getSetterExpr(toBeSetVal);
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) override {
    return retValFieldAccessor + " = " + getGetterExpr() + ";";
  }

  bool supports(FieldDecl *fd) override {
      if (fd->getType()->isArrayType()) return false;

      bool const doesSupport = supports(fd->getNameAsString(), fd->getType(), fd->attrs());
    if (doesSupport) return true;

    auto name = fd->getNameAsString();
    if (name.find("__truncate_mantissa_") != std::string::npos) return true;

      return false;
  }

  bool supports(std::string fieldName, QualType type, Attrs attrs) override {
    bool isFloatType = type->isFloatingType();
      if (!isFloatType) return false;
    for( auto *attr : attrs) {
      if (!llvm::isa<CompressTruncateMantissaAttr>(attr)) continue;
        return true;
    }

      if (fieldName.find("__truncate_mantissa_") != std::string::npos) return true;

      return false;
  }

};

#endif // CLANG_FLOATLIKEBITARRAYCOMPRESSOR_H
