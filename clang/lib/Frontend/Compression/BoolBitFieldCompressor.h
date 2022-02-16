//
// Created by p on 16/02/2022.
//

#ifndef CLANG_BOOLBITFIELDCOMPRESSOR_H
#define CLANG_BOOLBITFIELDCOMPRESSOR_H

class BoolBitFieldCompressor {

  std::string _fieldName;

public:

  explicit BoolBitFieldCompressor(std::string fieldName) : _fieldName(fieldName) {}

  bool isStoredInBitArray() {
    return false;
  }

  bool isStoredInSeparateField() {
    return true;
  }

  unsigned int getBitArrayStorageSize() {
    llvm::errs() << "getBitArrayStorageSize should not be called\n";
    return 0;
  }

  std::string getFieldDecl() {
    return "bool " + _fieldName + " : 1;";
  }

  std::string getGetterExpr(std::string thisAccessor) {
    return thisAccessor + _fieldName;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string valExpr) {
    return thisAccessor + _fieldName + " = " + valExpr;
  }

  std::string getCopyValueStmt(std::string fromObjAccessor, std::string toObjAccessor) {
    return toObjAccessor + _fieldName + " = " + fromObjAccessor + _fieldName;
  }

};

#endif // CLANG_BOOLBITFIELDCOMPRESSOR_H
