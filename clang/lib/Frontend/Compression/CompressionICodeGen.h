//
// Created by p on 23/01/2022.
//

#ifndef CLANG_COMPRESSIONICODEGEN_H
#define CLANG_COMPRESSIONICODEGEN_H

#include "../SemaIR/SemaIR.h"
#include "Bitfield/DelegatingFieldBitfieldCompressor.h"

class CompressionICodeGen {

public:
  virtual ~CompressionICodeGen() = default;

  virtual std::string getCompressedStructName() = 0;

  virtual std::string getFullyQualifiedCompressedStructName() = 0;

  virtual std::string getGlobalNsFullyQualifiedCompressedStructName() = 0;

  virtual std::unique_ptr<SemaRecordDecl> getSemaRecordDecl() = 0;

  virtual std::string getCompressedStructDef() = 0;

  virtual std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) = 0;

  virtual std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) = 0;

  virtual std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs) = 0;

  virtual std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs, std::string toBeSetValue) = 0;

};

bool isNonIndexAccessCompressionCandidate(FieldDecl *fd) {
  if (DelegatingNonIndexedFieldBitfieldCompressor().supports(fd)) return true;
  return false;
}

bool isIndexAccessCompressionCandidate(FieldDecl *fd) {
  if (ConstantSizeArrayBitfieldCompressor().supports(fd)) return true;
  return false;
}

bool isCompressionCandidate(FieldDecl *fieldDecl) {
  if (DelegatingFieldBitfieldCompressor().supports(fieldDecl)) return true;
  return false;
}

bool isCompressionCandidate(RecordDecl *recordDecl) {
  if (!recordDecl) return false; // null record cannot be compressed
  for (auto *field : recordDecl->fields()) {
    if (isCompressionCandidate(field)) return true;
  }
  return false;
}

QualType getTypeFromIndirectType(QualType type, std::string &ptrs) {
  while (type->isReferenceType() || type->isAnyPointerType()) {
    if (type->isReferenceType()) {
      type = type.getNonReferenceType();
      ptrs += "&";
    } else if (type->isAnyPointerType()) {
      type = type->getPointeeType();
      ptrs += "*";
    }
  }
  return type;
}

#endif // CLANG_COMPRESSIONICODEGEN_H
