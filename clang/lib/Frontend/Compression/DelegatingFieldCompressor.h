//
// Created by p on 16/02/2022.
//

#ifndef CLANG_DELEGATINGFIELDCOMPRESSOR_H
#define CLANG_DELEGATINGFIELDCOMPRESSOR_H

#include "DelegatingNonIndexedFieldCompressor.h"
#include "ConstantSizeArrayBitArrayCompressor.h"

class DelegatingFieldCompressor {

  std::unique_ptr<DelegatingNonIndexedFieldCompressor> _nonIndexedCompressor;
  std::unique_ptr<ConstantSizeArrayBitArrayCompressor> _indexedCompressor;

public:

  DelegatingFieldCompressor() {}

  DelegatingFieldCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, FieldDecl *fd) {
    if (DelegatingNonIndexedFieldCompressor().supports(fd)) {
      _nonIndexedCompressor = std::make_unique<DelegatingNonIndexedFieldCompressor>(tableCellSize, tableName, structName, fd);
    } else if (ConstantSizeArrayBitArrayCompressor().supports(fd)) {
      _indexedCompressor = std::make_unique<ConstantSizeArrayBitArrayCompressor>(tableCellSize, tableName, structName, fd);
    }
  }

  unsigned int getCompressedTypeWidth() {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getCompressedTypeWidth();
    if (_indexedCompressor) return _indexedCompressor->getCompressedTypeWidth();
  }

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getCopyConstructorStmt(thisAccessor, toBeSetVal);
    if (_indexedCompressor) return _indexedCompressor->getCopyConstructorStmt(thisAccessor, toBeSetVal);
  }

  bool supports(FieldDecl *fd) {
    return supports(fd->getType(), fd->attrs());
  }

  bool supports(QualType type, Attrs attrs) {
    if (DelegatingNonIndexedFieldCompressor().supports(type, attrs)) return true;
    if (ConstantSizeArrayBitArrayCompressor().supports(type, attrs)) return true;
    return false;
  }

};

#endif // CLANG_DELEGATINGFIELDCOMPRESSOR_H
