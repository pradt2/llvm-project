//
// Created by p on 16/02/2022.
//

#ifndef CLANG_CONSTANTSIZEARRAYBITARRAYCOMPRESSOR_H
#define CLANG_CONSTANTSIZEARRAYBITARRAYCOMPRESSOR_H

#include "AbstractBitArrayCompressor.h"
#include "DelegatingNonIndexedFieldCompressor.h"

class ConstantSizeArrayBitArrayCompressor : public AbstractBitArrayCompressor {
  std::vector<unsigned int> _dimensions;
  std::string _fieldName;
  std::string _structName;
  std::unique_ptr<NonIndexedFieldCompressor> _elementCompressor;

  unsigned int getTotalElements() {
    unsigned int counter = 1;
    for (auto dim : _dimensions) counter *= dim;
    return counter;
  }

  std::string getLinearItemIndex(std::vector<std::string> idxAccessors) {
    if (idxAccessors.size() != _dimensions.size()) {
      llvm::errs() << "Accessors idxs len != arr dims len\n";
      return 0;
    }
    std::string idx = "0";
    for (unsigned long i = 0; i < idxAccessors.size(); i++) {
      unsigned int multiplier = 1;
      for (unsigned long j = i + 1; j < _dimensions.size(); j++) {
        multiplier *= _dimensions[j];
      }
      idx += " + " + idxAccessors[i] + " * " + std::to_string(multiplier);
    }
    return "(" + idx + ")";
  }

  std::string getElementGetter(std::string thisAccessor, unsigned int elementIndex) {
    _elementCompressor->setOffset(_offset + elementIndex * _elementCompressor->getCompressedTypeWidth());
    return _elementCompressor->getGetterExpr(thisAccessor);
  }

  std::string getElementSetter(std::string thisAccessor, unsigned int elementIndex, std::string toBeSetValExpr) {
    _elementCompressor->setOffset(_offset + elementIndex * _elementCompressor->getCompressedTypeWidth());
    return _elementCompressor->getSetterExpr(thisAccessor, toBeSetValExpr);
  }

  QualType getElementType(const ConstantArrayType *arrType) {
    QualType elementType;
    while (true) {
      if (arrType->getElementType()->isConstantArrayType()) {
        arrType = llvm::cast<ConstantArrayType>(arrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        elementType = arrType->getElementType();
        break;
      }
    }
    return elementType;
  }

  void populateDimensions(const ConstantArrayType *arrType) {
    while (true) {
      _dimensions.push_back(arrType->getSize().getSExtValue());
      if (arrType->getElementType()->isConstantArrayType()) {
        arrType = llvm::cast<ConstantArrayType>(arrType->getElementType()->getAsArrayTypeUnsafe());
      } else {
        break;
      }
    }
  }

public:
  ConstantSizeArrayBitArrayCompressor() {}

  ConstantSizeArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, std::vector<unsigned int> dimensions, std::string fieldName, std::string structName, QualType elementType, Attrs attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _dimensions(dimensions), _fieldName(fieldName), _structName(structName) {}

  ConstantSizeArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string structName, FieldDecl *fd)
      : ConstantSizeArrayBitArrayCompressor(tableCellSize, tableName, fd->getNameAsString(), structName, fd->getType(), fd->attrs()) {}

  ConstantSizeArrayBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string fieldName, std::string structName, QualType type, Attrs attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()), _fieldName(fieldName), _structName(structName) {
    auto *arrType = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
    auto elementType = getElementType(arrType);
    _elementCompressor = std::make_unique<DelegatingNonIndexedFieldCompressor>(tableCellSize, tableName, structName, elementType, attrs);
  }

  unsigned int getCompressedTypeWidth() override {
    unsigned int elementCompressedTypeWidth = _elementCompressor->getCompressedTypeWidth();
    unsigned int totalElements = getTotalElements();
    return elementCompressedTypeWidth * totalElements;
  }

  std::string getGetterMethod() {
    std::vector<std::string> idxs;
    std::string method = _elementCompressor->getTypeName() + " get__" + _fieldName + "(";
    for (unsigned int i = 0; i < _dimensions.size(); i++) {
      std::string idx = "idx" + std::to_string(i);
      idxs.push_back(idx);
      method += "unsigned int " + idx + ", ";
    }
    method.pop_back();
    method.pop_back(); // removing the trailing ', '
    method += ") {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      "case " + std::to_string(i) + ": return " + getElementGetter("this->", i) + ";\n";
    }
    method += "default: return (" + _elementCompressor->getTypeName() + ") 0;\n";
    method += "}\n"; // close switch
    method += "}\n"; // close method
    return method;
  }

  std::string getSetterMethod() {
    std::vector<std::string> idxs;
    std::string method = "void set__" + _fieldName + "(";
    for (unsigned int i = 0; i < _dimensions.size(); i++) {
      std::string idx = "idx" + std::to_string(i);
      idxs.push_back(idx);
      method += "unsigned int " + idx + ", ";
    }
    method += _elementCompressor->getTypeName() + "val";
    method += ") {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      "case " + std::to_string(i) + ": " + getElementSetter("this->", i, "val") + "; break;\n";
    }
    method += "default: break;\n";
    method += "}\n"; // close switch
    method += "}\n"; // close method
    return method;
  }

  std::string getGetterExpr(std::string thisAccessor, std::vector<std::string> idxExprs) {
    std::string methodInvocation = thisAccessor + "get__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation.pop_back();
    methodInvocation.pop_back(); // remove trailing ', '
    methodInvocation += ")";
    return methodInvocation;
  }

  std::string getSetterExpr(std::string thisAccessor, std::vector<std::string> idxExprs, std::string toBeSetValue) {
    std::string methodInvocation = thisAccessor + "get__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation += toBeSetValue;
    methodInvocation += ")";
    return methodInvocation;
  }

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) {
    return "/** TODO to be implemented **/";
  }

  bool supports(FieldDecl *d) {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) {
    bool isConstSizeArr = type->isConstantArrayType();
    if (!isConstSizeArr) return false;
    if (DelegatingNonIndexedFieldCompressor().supports(getElementType(llvm::cast<ConstantArrayType>(type)), attrs)) {
      return true;
    }
    return false;
  }

};

#endif // CLANG_CONSTANTSIZEARRAYBITARRAYCOMPRESSOR_H
