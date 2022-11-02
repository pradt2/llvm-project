//
// Created by p on 23/01/2022.
//

#ifndef CLANG_COMPRESSIONCODEGENDELEGATE_H
#define CLANG_COMPRESSIONCODEGENDELEGATE_H

#include "Bitfield/CompressionBitfieldCodeGen.h"
#include "Bitshift/CompressionBitshiftCodeGen.h"

class CompressionCodeGenResolver : public CompressionICodeGen {

  std::unique_ptr<CompressionICodeGen> codeGen;

public:

  explicit CompressionCodeGenResolver(RecordDecl *d, ASTContext &Ctx,
                                                      SourceManager &SrcMgr,
                                                      LangOptions &LangOpts,
                                                      Rewriter &R) {
    for (auto *attr : d->attrs()) {
      if (!llvm::isa<CompressionMethodAttr>(attr))
        continue;
      auto *compressionMethodAttr = llvm::cast<CompressionMethodAttr>(attr);
      switch (compressionMethodAttr->getCompressionMethod()) {
      case CompressionMethodAttr::CompressionMethodType::Bitshift:
        this->codeGen = std::make_unique<CompressionBitshiftCodeGen>(d, Ctx, SrcMgr, LangOpts, R);
        return;
      case CompressionMethodAttr::CompressionMethodType::Bitpack:
        this->codeGen = std::make_unique<CompressionBitfieldCodeGen>(d, Ctx, SrcMgr, LangOpts, R);
        return;
      }
    }
    this->codeGen = std::make_unique<CompressionBitfieldCodeGen>(d, Ctx, SrcMgr, LangOpts, R);
  }

  std::string getCompressedStructName() override {
      return this->codeGen->getCompressedStructName();
  }

  std::string getFullyQualifiedCompressedStructName() override {
    return this->codeGen->getFullyQualifiedCompressedStructName();
  }

  std::string getGlobalNsFullyQualifiedCompressedStructName() override {
    return this->codeGen->getGlobalNsFullyQualifiedCompressedStructName();
  }

  std::unique_ptr<SemaRecordDecl> getSemaRecordDecl() override {
    return this->codeGen->getSemaRecordDecl();
  }

  std::string getCompressedStructDef() override {
      return this->codeGen->getCompressedStructDef();
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor) override {
    return this->codeGen->getGetterExpr(fieldDecl, thisAccessor);
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::string toBeSetValue) override {
    return this->codeGen->getSetterExpr(fieldDecl, thisAccessor, toBeSetValue);
  }

  std::string getGetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs) override {
    return this->codeGen->getGetterExpr(fieldDecl, thisAccessor, idxs);
  }

  std::string getSetterExpr(FieldDecl *fieldDecl, std::string thisAccessor, std::vector<std::string> idxs, std::string toBeSetValue) override {
    return this->codeGen->getSetterExpr(fieldDecl, thisAccessor, idxs, toBeSetValue);
  }

};

#endif // CLANG_COMPRESSIONCODEGENDELEGATE_H
