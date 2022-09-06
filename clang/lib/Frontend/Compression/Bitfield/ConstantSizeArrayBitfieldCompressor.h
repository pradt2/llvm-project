//
// Created by p on 16/02/2022.
//

#ifndef CLANG_CONSTANTSIZEARRAYBITFIELDCOMPRESSOR_H
#define CLANG_CONSTANTSIZEARRAYBITFIELDCOMPRESSOR_H

#include "DelegatingNonIndexedFieldBitfieldCompressor.h"
#include "../Utils.h"
#include <vector>

class ConstantSizeArrayBitfieldCompressor {
  std::vector<unsigned int> _dimensions;
  std::string _fieldName;
  std::string _structName;
  std::string thisAccessor;
  QualType elementType;
  Attrs attrs;
  unsigned int elementCompressedWidth;

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
      idx += " + " + idxAccessors[i] + " * " + to_constant(multiplier);
    }
    return "(" + idx + ")";
  }

  DelegatingNonIndexedFieldBitfieldCompressor getElementCompressor(unsigned int elementIdx) {
      return DelegatingNonIndexedFieldBitfieldCompressor(this->_structName,this->thisAccessor, this->_fieldName + std::to_string(elementIdx), this->elementType, this->attrs);
  }

  std::string getElementGetter(unsigned int elementIndex) {
    std::string elementGetter = this->getElementCompressor(elementIndex).getGetterExpr();
    return elementGetter;
  }

  std::string getElementSetter(unsigned int elementIndex, std::string toBeSetValExpr) {
    std::string elementSetter = this->getElementCompressor(elementIndex).getSetterExpr(toBeSetValExpr);
    return elementSetter;
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
  ConstantSizeArrayBitfieldCompressor() : attrs(llvm::SmallVector<Attr *, 4>()) {}

  ConstantSizeArrayBitfieldCompressor(std::string structName, std::string thisAccessor, FieldDecl *fd)
      : ConstantSizeArrayBitfieldCompressor(fd->getNameAsString(), structName, thisAccessor, fd->getType(), fd->attrs()) {}

  ConstantSizeArrayBitfieldCompressor(std::string fieldName, std::string structName, std::string thisAccessor, QualType type, Attrs attrs)
      :  _fieldName(fieldName), _structName(structName), thisAccessor(thisAccessor), attrs(attrs) {
    auto *arrType = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
    populateDimensions(arrType);
    this->elementType = getElementType(arrType);

    this->elementCompressedWidth = DelegatingNonIndexedFieldBitfieldCompressor(this->_structName, this->thisAccessor, this->_fieldName, this->elementType, this->attrs)
                                       .getCompressedTypeWidth();
  }

  static QualType getElementType(const ConstantArrayType *arrType) {
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

  unsigned int getTotalElements() {
    unsigned int counter = 1;
    for (auto dim : _dimensions) counter *= dim;
    return counter;
  }

  unsigned int getCompressedTypeWidth() {
    unsigned int totalElements = getTotalElements();
    return this->elementCompressedWidth * totalElements;
  }

  unsigned int getElementCompressedTypeWidth() {
    return this->elementCompressedWidth;
  }

  std::string getElementTypeStr() {
    std::string elementTypeStr = this->elementType.getAsString();
    if (elementTypeStr == "_Bool") return "bool";
    return elementTypeStr;
  }

  std::string getGetterMethod() {
    std::vector<std::string> idxs;
    std::string method = this->getElementTypeStr() + " get__" + _fieldName + "(";
    for (unsigned int i = 0; i < _dimensions.size(); i++) {
      std::string idx = "idx" + std::to_string(i);
      idxs.push_back(idx);
      method += "unsigned int " + idx + ", ";
    }
    method.pop_back();
    method.pop_back(); // removing the trailing ', '
    method += ") const {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      method += "case " + to_constant(i) + ": return " + getElementGetter(i) + ";\n";
    }
    method += "default: return (" + this->getElementTypeStr() + ") 0;\n";
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
    method += this->getElementTypeStr() + " val) {\n";
    method += "unsigned int linearIdx = " + getLinearItemIndex(idxs) + ";\n";
    method += "switch (linearIdx) {\n";
    unsigned int totalSize = getTotalElements();
    for (unsigned int i = 0; i < totalSize; i++) {
      method += "case " + to_constant(i) + ": " + getElementSetter(i, "val") + "; break;\n";
    }
    method += "default: break;\n";
    method += "}\n"; // close switch
    method += "}\n"; // close method
    return method;
  }

  std::string getGetterExpr(std::vector<std::string> idxExprs) {
    std::string methodInvocation = thisAccessor + "get__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation.pop_back();
    methodInvocation.pop_back(); // remove trailing ', '
    methodInvocation += ")";
    return methodInvocation;
  }

  std::string getSetterExpr(std::vector<std::string> idxExprs, std::string toBeSetValue) {
    std::string methodInvocation = thisAccessor + "set__" + _fieldName + "(";
    for (unsigned int i = 0; i < idxExprs.size(); i++) {
      methodInvocation += idxExprs[i] + ", ";
    }
    methodInvocation += toBeSetValue;
    methodInvocation += ")";
    return methodInvocation;
  }

  std::string getCopyConstructorStmt(std::string toBeSetVal) {
    llvm::errs() << "Copy constructor stmt for arrays is not implemented yet\n";
    return "/** copy constructor stmt for const size arr TO BE IMPLEMENTED */";
  }

  std::string getTypeCastToOriginalStmt(std::string retValFieldAccessor) {
    llvm::errs() << "Type cast to Original Type stmt for arrays is not implemented yet";
    return "/** type cast to original stmt for const size arr TO BE IMPLEMENTED */";
  }

  bool supports(FieldDecl *d) {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) {
    bool isConstSizeArr = type->isConstantArrayType();
    if (!isConstSizeArr) return false;
    auto *constSizeArr = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
    if (DelegatingNonIndexedFieldCompressor().supports(getElementType(constSizeArr), attrs)) {
      return true;
    }
    return false;
  }

};

#endif // CLANG_CONSTANTSIZEARRAYBITSHIFTCOMPRESSOR_H
