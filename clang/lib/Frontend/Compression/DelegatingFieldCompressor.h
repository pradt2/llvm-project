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

  std::string getTypeCastToOriginalStmt(std::string thisAccessor, std::string retValFieldAccessor) {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getTypeCastToOriginalStmt(thisAccessor, retValFieldAccessor);
    if (_indexedCompressor) return _indexedCompressor->getCopyConstructorStmt(thisAccessor, retValFieldAccessor);
  }

  void setOffset(unsigned int offset) {
    if (_nonIndexedCompressor) _nonIndexedCompressor->setOffset(offset);
    if (_indexedCompressor) _indexedCompressor->setOffset(offset);
  }

  bool supports(FieldDecl *fd) {
    bool doesSupport = supports(fd->getType(), fd->attrs());

    if (doesSupport) return doesSupport;

    for ( auto *attr : fd->attrs()) {
      if (llvm::isa<CompressAttr>(attr)
          || llvm::isa<CompressRangeAttr>(attr)
          || llvm::isa<CompressTruncateMantissaAttr>(attr)) {
        llvm::errs() << "Packing requested for an unsupported field type! " << "Packing will not be implemented for " << fd->getSourceRange().getBegin().printToString(fd->getASTContext().getSourceManager()) << "\n";
        exit(1);
      }
    }

    return doesSupport;
  }

  bool supports(QualType type, Attrs attrs) {
    if (DelegatingNonIndexedFieldCompressor().supports(type, attrs)) return true;
    if (ConstantSizeArrayBitArrayCompressor().supports(type, attrs)) return true;

    return false;
  }

};

#endif // CLANG_DELEGATINGFIELDCOMPRESSOR_H
