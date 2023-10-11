//
// Created by p on 16/02/2022.
//

#ifndef CLANG_NONINDEXEDFIELDBITFIELDCOMPRESSOR_H
#define CLANG_NONINDEXEDFIELDBITFIELDCOMPRESSOR_H

typedef llvm::iterator_range<Decl::attr_iterator> Attrs;

class NonIndexedFieldBitfieldCompressor {
public:
  virtual ~NonIndexedFieldBitfieldCompressor() = default;
  virtual bool supports(FieldDecl *fd) = 0;
  virtual bool supports(std::string fieldName, QualType type, Attrs attrs) = 0;
  virtual unsigned int getCompressedTypeWidth() = 0;
  virtual std::string getTypeName() = 0;
  virtual std::string getGetterExpr() = 0;
  virtual std::string getSetterExpr(std::string toBeSetValue) = 0;
  virtual std::string getCopyConstructorStmt(std::string toBeSetVal) = 0;
  virtual std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) = 0;
};

#endif // CLANG_NONINDEXEDFIELDCOMPRESSOR_H
