//
// Created by p on 16/02/2022.
//

#ifndef CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H
#define CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H

#include "BoolBitArrayCompressor.h"
#include "EnumBitArrayCompressor.h"
#include "FloatLikeBitArrayCompressor.h"
#include "IntLikeBitArrayCompressor.h"

class DelegatingNonIndexedFieldCompressor : public NonIndexedFieldCompressor {

  std::unique_ptr<NonIndexedFieldCompressor> _delegate;

public:

  DelegatingNonIndexedFieldCompressor() {}

  DelegatingNonIndexedFieldCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, FieldDecl *fd)
      : DelegatingNonIndexedFieldCompressor(tableSpec, offset, structName, fd->getType(), fd->attrs()) {}

  DelegatingNonIndexedFieldCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, QualType type, Attrs attrs) {
    if (BoolBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<BoolBitArrayCompressor>(tableSpec, offset);
    } else if (IntLikeBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<IntLikeBitArrayCompressor>(tableSpec, offset, type, attrs);
    } else if (EnumBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<EnumBitArrayCompressor>(tableSpec, offset, type);
    } else if (FloatLikeBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<FloatLikeBitArrayCompressor>(tableSpec, offset, structName, type, attrs);
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
    if (DelegatingNonIndexedFieldCompressor({}, {}, "mock", type, attrs)._delegate) return true;
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
