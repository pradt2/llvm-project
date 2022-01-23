//
// Created by p on 22/01/2022.
//

#ifndef CLANG_COMPRESSIONBITPACKCODEGEN_H
#define CLANG_COMPRESSIONBITPACKCODEGEN_H

#include "./CompressionICodeGen.h"

class CompressionBitpackCodeGen : public CompressionICodeGen {

  RecordDecl *decl;
  CompilerInstance &CI;

  unsigned int getCompressedTypeWidth(FieldDecl *fieldDecl) {
    auto type = fieldDecl->getType();
    if (type->isBooleanType()) return 1;
    if (type->isEnumeralType()) {
      return getEnumTypeWidth(const_cast<EnumDecl *>(type->getAs<EnumType>()->getDecl()));
    }
    for (auto *attr : fieldDecl->attrs()) {
      if (attr->getKind() != clang::attr::CompressRange) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      int minValue = compressRangeAttr->getMinValue();
      int maxValue = compressRangeAttr->getMaxValue();
      unsigned int valueRange = abs(maxValue - minValue);
      if (valueRange == 1) return 1;
      unsigned size = ceil(log2(valueRange));
      return size;
    }
    return 64;
  }

  unsigned int getEnumTypeWidth(EnumDecl *enumDecl) {
    long minValue = LONG_MAX;
    long maxValue = LONG_MIN;
    for (auto *x : enumDecl->enumerators()) {
      long enumConstantValue = x->getInitVal().getSExtValue();
      if (enumConstantValue < minValue) minValue = enumConstantValue;
      if (enumConstantValue > maxValue) maxValue = enumConstantValue;
    }
    unsigned int bitsWidth = ceil(log2(maxValue - minValue + 1));
    return bitsWidth;
  }

  int getCompressionConstant(FieldDecl *fieldDecl) {
    for (auto *attr : fieldDecl->attrs()) {
      if (attr->getKind() != clang::attr::CompressRange) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      int minValue = compressRangeAttr->getMinValue();
      return minValue;
    }
    if (!fieldDecl->getType()->isEnumeralType()) return 0;
    long minValue = LONG_MAX;
    for (auto *x : fieldDecl->getType()->getAs<EnumType>()->getDecl()->enumerators()) {
      long enumConstantValue = x->getInitVal().getSExtValue();
      if (enumConstantValue < minValue) minValue = enumConstantValue;
    }
    return minValue == LONG_MAX ? 0 : minValue;
  }

  std::string getFieldsDecl() {
    std::string fields;
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        SourceRange typeAndName = SourceRange(field->getSourceRange().getBegin(), field->getLocation().getLocWithOffset(field->getNameAsString().size()).getLocWithOffset(-1));
        unsigned int compressedBitSize = getCompressedTypeWidth(field);
        std::string fieldStr = CI.getSourceManager().getRewriter()->getRewrittenText(typeAndName) + " : " + std::to_string(compressedBitSize);
        fields += fieldStr + "; ";
      } else {
        fields += CI.getSourceManager().getRewriter()->getRewrittenText(field->getSourceRange()) + "; ";
      }
    }
    return fields;
  }

  std::string getEmptyConstructor() {
    std::string constructor = getCompressedStructName();
    constructor += "() {}";
    return constructor;
  }

  std::string getFromOriginalTypeConstructor() {
    std::string constructor = getCompressedStructName();
    std::string localVarName = "__arg0";
    constructor += " (" + getOriginalStructName() + " &&" + localVarName + ") { ";
    for (auto *field : decl->fields()) {
      if (isCompressionCandidate(field)) {
        constructor += getSetterExpr(field, "this->", localVarName + "." + field->getNameAsString()) + "; ";
      }
      else {
        constructor += "this->" + field->getNameAsString() + " = " + localVarName + "." + field->getNameAsString() + "; ";
      }
    }
    constructor += "} ";
    return constructor;
  }

  std::string getTypeCastToOriginal() {
    std::string typeCast = "operator " + getOriginalStructName() + "() { return { ";
    for (auto *field : decl->fields()) {
      typeCast += getGetterExpr(field, "this->") + ", ";
    }
    typeCast += " }; }";
    return typeCast;
  }

  std::string getOriginalStructName() {
    return decl->getNameAsString();
  }

public:
  explicit CompressionBitpackCodeGen(RecordDecl *d, CompilerInstance &CI)
      : decl(d), CI(CI) {}

  std::string getCompressedStructName() override {
    return getOriginalStructName() + "__BITPACKED";
  }

  std::string getCompressedStructDef() override {
    std::string structName = getCompressedStructName();
    std::string fieldsDecl = getFieldsDecl();
    std::string emptyConstructor = getEmptyConstructor();
    std::string fromOriginalConstructor = getFromOriginalTypeConstructor();
    std::string typeCastToOriginal = getTypeCastToOriginal();
    std::string structDef = "\n#pragma pack(push, 1)\nstruct " + structName +
                            " { " + fieldsDecl + "; " + emptyConstructor +
                            "; " + fromOriginalConstructor + "; " +
                            typeCastToOriginal + "; };\n#pragma pack(pop)\n";
    return structDef;
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) override {
    int compressionConstant = getCompressionConstant(fieldDecl);
    if (compressionConstant == 0) return thisAccessor + fieldDecl->getNameAsString();
    std::string typeStr;
    return "static_cast<" + fieldDecl->getType().getAsString() + ">(" + thisAccessor + fieldDecl->getNameAsString() + " + " + std::to_string(compressionConstant) + ")";
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor,
                            std::string toBeSetValue) override {
    int compressionConstant = getCompressionConstant(fieldDecl);
    if (compressionConstant == 0) return thisAccessor + fieldDecl->getNameAsString() + " = " + toBeSetValue;
    std::string setterStmt = thisAccessor + fieldDecl->getNameAsString() + " = ((" + toBeSetValue + ") - " + std::to_string(compressionConstant) + ")";
    return setterStmt;
  }
};

#endif // CLANG_COMPRESSIONBITPACKCODEGEN_H
