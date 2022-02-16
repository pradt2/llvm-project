//
// Created by p on 16/02/2022.
//

#ifndef CLANG_NONINDEXEDFIELDCOMPRESSOR_H
#define CLANG_NONINDEXEDFIELDCOMPRESSOR_H

typedef llvm::iterator_range<Decl::attr_iterator> Attrs;

class NonIndexedFieldCompressor {
public:
  virtual ~NonIndexedFieldCompressor() = default;
  virtual bool supports(FieldDecl *fd) = 0;
  virtual bool supports(QualType type, Attrs attrs) = 0;
  virtual unsigned int getCompressedTypeWidth() = 0;
  virtual void setOffset(unsigned int offset) = 0;
  virtual std::string getTypeName() = 0;
  virtual std::string getGetterExpr(std::string thisAccessor) = 0;
  virtual std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) = 0;
  virtual std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) = 0;
};

#endif // CLANG_NONINDEXEDFIELDCOMPRESSOR_H
