//
// Created by p on 16/02/2022.
//

#ifndef CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H
#define CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H

#include "BoolBitArrayCompressor.h"
#include "IntLikeBitArrayCompressor.h"
#include "EnumBitArrayCompressor.h"
#include "FloatLikeBitArrayCompressor.h"

class DelegatingNonIndexedFieldCompressor : public NonIndexedFieldCompressor {

  std::unique_ptr<NonIndexedFieldCompressor> _delegate;

public:

  DelegatingNonIndexedFieldCompressor() {}

  DelegatingNonIndexedFieldCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, FieldDecl *fd)
      : DelegatingNonIndexedFieldCompressor(tableCellSize, tableName, structName, fd->getType(), fd->attrs()) {}

  DelegatingNonIndexedFieldCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, QualType type, Attrs attrs) {
    if (BoolBitArrayCompressor(TableSpec(), TableArea())
            .supports(type, attrs)) {
      _delegate = std::make_unique<BoolBitArrayCompressor>(tableCellSize, tableName);
    } else if (IntLikeBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<IntLikeBitArrayCompressor>(tableCellSize, tableName, type, attrs);
    } else if (EnumBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<EnumBitArrayCompressor>(tableCellSize, tableName, type);
    } else if (FloatLikeBitArrayCompressor().supports(type, attrs)) {
      _delegate = std::make_unique<FloatLikeBitArrayCompressor>(tableCellSize, tableName, structName, type, attrs);
    }
  }

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) override {
    return _delegate->getCopyConstructorStmt(thisAccessor, toBeSetVal);
  }

  std::string getTypeCastToOriginalStmt(std::string thisAccessor, std::string retValFieldAccessor) override {
    return _delegate->getTypeCastToOriginalStmt(thisAccessor, retValFieldAccessor);
  }

  void setOffset(unsigned int offset) override {
    _delegate->setOffset(offset);
  }

  unsigned int getCompressedTypeWidth() override {
    return _delegate->getCompressedTypeWidth();
  }

  std::string getTypeName() override { return _delegate->getTypeName(); }

  bool supports(FieldDecl *fd) override {
    return supports(fd->getType(), fd->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    if (DelegatingNonIndexedFieldCompressor(8, "mock", "mock", type, attrs)._delegate) return true;
    return false;
  }

  std::string getGetterExpr(std::string thisAccessor) override {
    return _delegate->getGetterExpr(thisAccessor);
  }

  std::string getSetterExpr(std::string thisAccessor,
                            std::string toBeSetValue) override {
    return _delegate->getSetterExpr(thisAccessor, toBeSetValue);
  }
};

#endif // CLANG_DELEGATINGNONINDEXEDFIELDCOMPRESSOR_H
