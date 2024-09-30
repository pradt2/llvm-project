#pragma once

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include "CompressionICodeGen.h"

const char *TEMPLATES = "#include <algorithm>\n"
                        "\n"
                        "template<typename T, unsigned BITS, unsigned ALIGNMENT, bool LSB>\n"
                        "class alignas(ALIGNMENT) CompressedVal {\n"
                        "    using u64 = unsigned long;\n"
                        "    \n"
                        "    __attribute__((packed)) u64 data: BITS;\n"
                        "\n"
                        "protected:\n"
                        "    constexpr inline T __get() const {\n"
                        "        union {\n"
                        "            u64 num;\n"
                        "            T orig;\n"
                        "        };\n"
                        "\n"
                        "        if constexpr (LSB) num = this->data;\n"
                        "        else num = this->data << (sizeof(T) * 8 - BITS);\n"
                        "\n"
                        "        return orig;\n"
                        "    }\n"
                        "\n"
                        "    constexpr inline void __set(T val) {\n"
                        "        union {\n"
                        "            u64 num;\n"
                        "            T orig;\n"
                        "        };\n"
                        "\n"
                        "        orig = val;\n"
                        "\n"
                        "        if constexpr (LSB) this->data = num;\n"
                        "        else this->data = num >> (sizeof(T) * 8 - BITS);\n"
                        "    }\n"
                        "\n"
                        "public:\n"
                        "    constexpr inline CompressedVal() = default;\n"
                        "\n"
                        "    constexpr inline CompressedVal(const CompressedVal &copy) = default;\n"
                        "\n"
                        "    constexpr inline CompressedVal(const T &value) {\n"
                        "        this->__set(value);\n"
                        "    }\n"
                        "\n"
                        "    constexpr inline CompressedVal &operator=(const CompressedVal &other) = default;\n"
                        "\n"
                        "    constexpr inline operator T() const {\n"
                        "        return (T) this->__get();\n"
                        "    }\n"
                        "};\n"
                        "\n"
                        "template<typename T, unsigned BITS = sizeof(T) * 8, unsigned ALIGNMENT = std::max(1U, std::__bit_ceil(BITS) / 8)>\n"
                        "class alignas(ALIGNMENT) CompressedNum : public CompressedVal<T, BITS, ALIGNMENT, std::is_integral_v<T>> {\n"
                        "    using Base = CompressedVal<T, BITS, ALIGNMENT, std::is_integral_v<T>>;\n"
                        "\n"
                        "public:\n"
                        "    using Base::Base;\n"
                        "\n"
                        "    template<typename X>\n"
                        "    constexpr inline CompressedNum &operator=(const X &value) {\n"
                        "        this->__set((T) value);\n"
                        "        return *this;\n"
                        "    }\n"
                        "\n"
                        "#define UNARY_PREFIX_OP(op)                         \\\n"
                        "    constexpr inline CompressedNum &operator op() { \\\n"
                        "        auto f = this->__get();                     \\\n"
                        "        f op;                                       \\\n"
                        "        this->__set(f);                             \\\n"
                        "        return *this;                               \\\n"
                        "    }                                               \\\n"
                        "\n"
                        "    UNARY_PREFIX_OP(++)\n"
                        "    UNARY_PREFIX_OP(--)\n"
                        "\n"
                        "#undef UNARY_PREFIX_OP\n"
                        "\n"
                        "#define UNARY_POSTFIX_OP(op)                          \\\n"
                        "    constexpr inline CompressedNum operator op(int) { \\\n"
                        "        auto oldThis = CompressedNum(*this);          \\\n"
                        "        this->operator op();                          \\\n"
                        "        return oldThis;                               \\\n"
                        "    }                                                 \\\n"
                        "\n"
                        "    UNARY_POSTFIX_OP(++)\n"
                        "    UNARY_POSTFIX_OP(--)\n"
                        "\n"
                        "#undef UNARY_POSTFIX_OP\n"
                        "\n"
                        "};\n"
                        "\n"
                        "template<typename T, unsigned BITS = sizeof(T) * 8, unsigned ALIGNMENT = std::max(1U, std::__bit_ceil(BITS) / 8)>\n"
                        "class alignas(ALIGNMENT) CompressedEnum : public CompressedVal<T, BITS, ALIGNMENT, true> {\n"
                        "    using Base = CompressedVal<T, BITS, ALIGNMENT, true>;\n"
                        "\n"
                        "public:\n"
                        "    using Base::Base;\n"
                        "\n"
                        "    constexpr inline CompressedEnum(int val) {\n"
                        "        this->__set((T) val);\n"
                        "    }\n"
                        "\n"
                        "    template<typename X>\n"
                        "    constexpr inline CompressedEnum &operator=(const X &value) {\n"
                        "        this->__set((T) value);\n"
                        "        return *this;\n"
                        "    }\n"
                        "};\n"
                        "\n"
                        "namespace std {\n"
                        "    template<typename _Tp1, typename _Tp2>\n"
                        "    _GLIBCXX_NODISCARD _GLIBCXX14_CONSTEXPR\n"
                        "    inline const _Tp2\n"
                        "    min(const _Tp1 __a, const _Tp2 __b) {\n"
                        "        if (__b < __a)\n"
                        "            return __b;\n"
                        "        return __a;\n"
                        "    }\n"
                        "\n"
                        "    template<typename _Tp1, typename _Tp2>\n"
                        "    _GLIBCXX_NODISCARD _GLIBCXX14_CONSTEXPR\n"
                        "    inline const _Tp2\n"
                        "    max(const _Tp1 __a, const _Tp2 __b) {\n"
                        "        if (__a < __b)\n"
                        "            return __b;\n"
                        "        return __a;\n"
                        "    }\n"
                        "}\n"
                        "\n";

class TemplateBasedCompressionRewriter: public ASTConsumer, public RecursiveASTVisitor<TemplateBasedCompressionRewriter> {
private:
  ASTContext &Ctx;
  SourceManager &SrcMgr;
  LangOptions &LangOpts;
  Rewriter &R;

  bool anyCompressionCandidatesFound = false;

  int getTruncateMantissaBits(FieldDecl *decl) {
    int mantissaBits = -1;
    for ( auto *attr : decl->attrs()) {
      if (llvm::isa<CompressTruncateMantissaAttr>(attr)) {
        mantissaBits = llvm::cast<CompressTruncateMantissaAttr>(attr)->getMantissaSize();
      }
    }

    if (mantissaBits < 0) {
      llvm::errs() << "Float compression candidate doesn't have the compression attribute\n";
      llvm::errs() << "Field name " << decl->getNameAsString() << " , parent type: " << decl->getParent()->getNameAsString() << "\n";
      exit(1);
    }

    return mantissaBits;
  }

  void getNewFloatingType(QualType type, char* buf, int mantissa) {
    const char *nativeTypeName;
    switch (type->getAs<clang::BuiltinType>()->getKind()) {
    case BuiltinType::Float : nativeTypeName = "float"; break;
    case BuiltinType::Double : nativeTypeName = "double"; break;
    case BuiltinType::LongDouble: nativeTypeName = "long double"; break;
    default:
      llvm::errs() << "Unsupported floating point type for compression\n";
      exit(1);
    }

    sprintf(buf, "CompressedNum<%s, %d>", nativeTypeName, mantissa);
  }

  void addTemplates() {
    auto tuBeginLoc = SrcMgr.getLocForStartOfFile(SrcMgr.getMainFileID());
    R.InsertText(tuBeginLoc, TEMPLATES);
  }

  void handleInt(FieldDecl *decl) {

  }

  void handleIntArr(FieldDecl *decl, QualType elementType) {

  }

  void handleEnum(FieldDecl *decl) {

  }

  void handleEnumArr(FieldDecl *decl, QualType elementType) {

  }

  void handleFloat(FieldDecl *decl) {
    auto mantissaBits = this->getTruncateMantissaBits(decl);
    char buf[128];
    this->getNewFloatingType(decl->getType(), buf, mantissaBits);

    auto typeLoc = decl->getTypeSourceInfo()->getTypeLoc();
    R.ReplaceText(typeLoc.getSourceRange(), llvm::StringRef(buf));
  }

  void handleFloatArr(FieldDecl *decl, QualType elementType) {
    auto mantissaBits = this->getTruncateMantissaBits(decl);
    char buf[128];
    this->getNewFloatingType(elementType, buf, mantissaBits);

    auto beginLoc = decl->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
    auto endLoc = decl->getLocation().getLocWithOffset(-1);
    R.ReplaceText(SourceRange(beginLoc, endLoc), llvm::StringRef(buf));
  }

public:
  explicit TemplateBasedCompressionRewriter(ASTContext &Ctx,
                                     SourceManager &SrcMgr,
                                     LangOptions &LangOpts,
                                     Rewriter &R) : Ctx(Ctx), SrcMgr(SrcMgr), LangOpts(LangOpts), R(R) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    TranslationUnitDecl *D = Context.getTranslationUnitDecl();
    TraverseDecl(D);
    if (this->anyCompressionCandidatesFound) this->addTemplates();
  }

  bool VisitFieldDecl(FieldDecl *decl) {
    if (!isCompressionCandidate(decl)) return true;
    this->anyCompressionCandidatesFound = true;
    auto type = decl->getType();

    bool isArr = type->isConstantArrayType();

    while (type->isConstantArrayType()) {
      auto *arrType = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
      type = arrType->getElementType();
    }

    if (isArr) {
      if (type->isIntegerType()) this->handleIntArr(decl, type);
      else if (type->isIntegralOrEnumerationType()) this->handleEnumArr(decl, type);
      else if (type->isRealFloatingType()) this->handleFloatArr(decl, type);
      return true;
    }

    if (type->isIntegerType()) this->handleInt(decl);
    else if (type->isIntegralOrEnumerationType()) this->handleEnum(decl);
    else if (type->isRealFloatingType()) this->handleFloat(decl);
    return true;
  }
};
