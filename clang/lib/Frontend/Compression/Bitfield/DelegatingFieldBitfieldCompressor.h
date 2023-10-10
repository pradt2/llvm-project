//
// Created by p on 16/02/2022.
//

#ifndef CLANG_DELEGATINGFIELDBITFIELDCOMPRESSOR_H
#define CLANG_DELEGATINGFIELDBITFIELDCOMPRESSOR_H

#include "DelegatingNonIndexedFieldBitfieldCompressor.h"
#include "ConstantSizeArrayBitfieldCompressor.h"

class DelegatingFieldBitfieldCompressor {

  std::unique_ptr<DelegatingNonIndexedFieldBitfieldCompressor> _nonIndexedCompressor;
  std::unique_ptr<ConstantSizeArrayBitfieldCompressor> _indexedCompressor;

public:

  DelegatingFieldBitfieldCompressor() {}

  DelegatingFieldBitfieldCompressor(std::string structName, std::string thisAccessor, FieldDecl *fd) {
    if (DelegatingNonIndexedFieldBitfieldCompressor().supports(fd)) {
      _nonIndexedCompressor = std::make_unique<DelegatingNonIndexedFieldBitfieldCompressor>(structName, thisAccessor, fd);
    } else if (ConstantSizeArrayBitfieldCompressor().supports(fd)) {
      _indexedCompressor = std::make_unique<ConstantSizeArrayBitfieldCompressor>(structName, thisAccessor, fd);
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
    if (_indexedCompressor) return _indexedCompressor->getTypeCastToOriginalStmt(retValFieldAccessor);
    llvm::errs() << "std::string getTypeCastToOriginalStmt() invalid";
    exit(1);
  }

  bool supports(FieldDecl *fd) {
      if (DelegatingNonIndexedFieldBitfieldCompressor().supports(fd)) return true;
      if (ConstantSizeArrayBitfieldCompressor().supports(fd)) return true;

    for ( auto *attr : fd->attrs()) {
      if (llvm::isa<CompressAttr>(attr)
          || llvm::isa<CompressRangeAttr>(attr)
          || llvm::isa<CompressTruncateMantissaAttr>(attr)) {
        llvm::errs() << "Packing requested for an unsupported field type! " << "Packing will not be implemented for " << fd->getSourceRange().getBegin().printToString(fd->getASTContext().getSourceManager()) << "\n";
        exit(1);
      }
    }

    return false;
  }

};

#endif // CLANG_DELEGATINGFIELDCOMPRESSOR_H
