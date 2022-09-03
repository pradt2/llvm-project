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

  DelegatingFieldCompressor(TableSpec tableSpec, unsigned int offset, std::string structName, std::string thisAccessor, FieldDecl *fd) {
    if (DelegatingNonIndexedFieldCompressor().supports(fd)) {
      _nonIndexedCompressor = std::make_unique<DelegatingNonIndexedFieldCompressor>(tableSpec, offset, structName, fd);
    } else if (ConstantSizeArrayBitArrayCompressor().supports(fd)) {
      _indexedCompressor = std::make_unique<ConstantSizeArrayBitArrayCompressor>(tableSpec, offset, structName, thisAccessor, fd);
    }
  }

  unsigned int getCompressedTypeWidth() {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getCompressedTypeWidth();
    if (_indexedCompressor) return _indexedCompressor->getCompressedTypeWidth();
    return -1;
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getCopyConstructorStmt(toBeSetVal);
    if (_indexedCompressor) return _indexedCompressor->getCopyConstructorStmt(toBeSetVal);
    llvm::errs() << "std::string getCopyConstructorStmt() invalid";
    exit(1);
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) {
    if (_nonIndexedCompressor) return _nonIndexedCompressor->getTypeCastToOriginalStmt(retValFieldAccessor);
    if (_indexedCompressor) return _indexedCompressor->getCopyConstructorStmt(retValFieldAccessor);
    llvm::errs() << "std::string getTypeCastToOriginalStmt() invalid";
    exit(1);
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
