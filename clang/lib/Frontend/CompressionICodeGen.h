//
// Created by p on 23/01/2022.
//

#ifndef CLANG_COMPRESSIONICODEGEN_H
#define CLANG_COMPRESSIONICODEGEN_H

using namespace clang;

class CompressionICodeGen {

public:
  virtual ~CompressionICodeGen() = default;

  virtual std::string getCompressedStructName() = 0;

  virtual std::string getCompressedStructDef() = 0;

  virtual std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) = 0;

  virtual std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) = 0;

  virtual std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs) = 0;

  virtual std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs, std::string toBeSetValue) = 0;

};

#endif // CLANG_COMPRESSIONICODEGEN_H
