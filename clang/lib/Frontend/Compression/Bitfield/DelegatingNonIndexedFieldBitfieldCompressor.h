//
// Created by p on 16/02/2022.
//

#ifndef CLANG_DELEGATINGNONINDEXEDFIELDBITFIELDCOMPRESSOR_H
#define CLANG_DELEGATINGNONINDEXEDFIELDBITFIELDCOMPRESSOR_H

#include "NonIndexedFieldBitfieldCompressor.h"
#include "BoolBitfieldCompressor.h"
#include "EnumBitfieldCompressor.h"
#include "FloatLikeBitfieldCompressor.h"
#include "IntLikeBitfieldCompressor.h"

class DelegatingNonIndexedFieldBitfieldCompressor : public NonIndexedFieldBitfieldCompressor {

  std::unique_ptr<NonIndexedFieldBitfieldCompressor> _delegate;

public:

  DelegatingNonIndexedFieldBitfieldCompressor() {}

  DelegatingNonIndexedFieldBitfieldCompressor(std::string structName, std::string thisAccessor, FieldDecl *fd)
      : DelegatingNonIndexedFieldBitfieldCompressor(structName, thisAccessor, fd->getNameAsString(), fd->getType(), fd->attrs()) {}

  DelegatingNonIndexedFieldBitfieldCompressor(std::string structName, std::string thisAccessor, std::string fieldName, QualType type, Attrs attrs) {
    if (BoolBitfieldCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<BoolBitfieldCompressor>(thisAccessor, fieldName);
    } else if (IntLikeBitfieldCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<IntLikeBitfieldCompressor>(thisAccessor, fieldName, type, attrs);
    } else if (EnumBitfieldCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<EnumBitfieldCompressor>(thisAccessor, fieldName, type);
    } else if (FloatLikeBitfieldCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<FloatLikeBitfieldCompressor>(structName, thisAccessor, fieldName, type, attrs);
    }
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) override {
    return _delegate->getCopyConstructorStmt(toBeSetVal);
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) override {
    return _delegate->getTypeCastToOriginalStmt(retValFieldAccessor);
  }

  unsigned int getCompressedTypeWidth() override {
    return _delegate->getCompressedTypeWidth();
  }

  std::string getTypeName() override { return _delegate->getTypeName(); }

  bool supports(FieldDecl *fd) override {
    return supports(fd->getType(), fd->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    if (DelegatingNonIndexedFieldBitfieldCompressor("mock", "mock", "mock", type, attrs)._delegate) return true;
    return false;
  }

  std::string getGetterExpr() override {
    return _delegate->getGetterExpr();
  }

  std::string getSetterExpr(std::string toBeSetValue) override {
    return _delegate->getSetterExpr(toBeSetValue);
  }
};

#endif // CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H
