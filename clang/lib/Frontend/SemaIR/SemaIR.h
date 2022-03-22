//
// Created by p on 10/03/2022.
//

#ifndef CLANG_SEMAIR_H
#define CLANG_SEMAIR_H

using namespace clang;

struct SemaFieldDecl;
struct SemaRecordDecl;
std::unique_ptr<SemaRecordDecl> fromRecordDecl(RecordDecl *decl);

struct SemaType {
  virtual bool isPrimitiveType() { return false; }
  virtual bool isEnumType() { return false; }
  virtual bool isConstSizeArrType() { return false; }
  virtual bool isRecordType() { return false; }
  virtual ~SemaType() = default;
};

struct SemaPrimitiveType : SemaType {
  BuiltinType::Kind typeKind;
  bool isPrimitiveType() override { return true; }
  static SemaPrimitiveType getForKind(BuiltinType::Kind kind) {
    SemaPrimitiveType type;
    type.typeKind = kind;
    return type;
  }
};

struct SemaEnumValue {
  std::string name;
  long value;
};

struct SemaEnumType : SemaType {
  std::string name;
  SemaPrimitiveType integerType;
  std::vector<SemaEnumType> values;
  bool isEnumType() override { return true; }
};

struct SemaConstSizeArrType : SemaType {
  unsigned int size;
  std::unique_ptr<SemaType> elementType;
  bool isConstSizeArrType() override { return true; }
};

struct SemaRecordType : SemaType {
  std::unique_ptr<SemaRecordDecl> recordDecl;
  bool isRecordType() override { return true; }
};

std::unique_ptr<SemaType> fromQualType(QualType type) {
  if (type->isBuiltinType()) {
    std::unique_ptr<SemaPrimitiveType> semaType = std::make_unique<SemaPrimitiveType>();
    auto *builtinType = type->getAs<BuiltinType>();
    semaType->typeKind = builtinType->getKind();
    return semaType;
  }
  if (type->isEnumeralType()) {
    std::unique_ptr<SemaEnumType> semaType = std::make_unique<SemaEnumType>();
    EnumDecl *enumDecl = type->getAs<EnumType>()->getDecl();
    semaType->name = enumDecl->getNameAsString();
    semaType->integerType = *((SemaPrimitiveType*) fromQualType(enumDecl->getIntegerType()).release());
    return semaType;
  }
  if (type->isConstantArrayType()) {
    auto *constArr = llvm::cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
    std::unique_ptr<SemaConstSizeArrType> semaType = std::make_unique<SemaConstSizeArrType>();
    semaType->size = constArr->getSize().getZExtValue();
    semaType->elementType = fromQualType(constArr->getElementType());
    return semaType;
  }
  if (type->isRecordType()) {
    std::unique_ptr<SemaRecordType> semaType = std::make_unique<SemaRecordType>();
    semaType->recordDecl = fromRecordDecl(type->getAsRecordDecl());
    return semaType;
  }
  llvm::errs() << "Unsupported type for SemaIR representation " << __FILE__ << ":" << __LINE__ << "\n";
  type->dump();
}

struct SemaFieldDecl {
  std::string name;
  SemaRecordDecl* parent;
  std::unique_ptr<SemaType> type;
};

std::unique_ptr<SemaFieldDecl> fromFieldDecl(SemaRecordDecl &parent, FieldDecl *decl) {
  auto semaFieldDecl = std::make_unique<SemaFieldDecl>();
  semaFieldDecl->name = decl->getNameAsString();
  semaFieldDecl->parent = &parent;
  semaFieldDecl->type = fromQualType(decl->getType());
  return semaFieldDecl;
}

struct SemaRecordDecl {
  std::string name;
  std::string fullyQualifiedName;
  std::vector<std::unique_ptr<SemaFieldDecl>> fields;
};

std::unique_ptr<SemaRecordDecl> fromRecordDecl(RecordDecl *decl) {
  auto semaRecordDecl = std::make_unique<SemaRecordDecl>();
  std::string name = decl->getASTContext().getTypeDeclType(decl).getAsString();
  semaRecordDecl->name = name;
  semaRecordDecl->fullyQualifiedName = name;
//  if (llvm::isa<ClassTemplateSpecializationDecl>(decl)) {
//    ClassTemplateSpecializationDecl *specDecl = llvm::cast<ClassTemplateSpecializationDecl>(decl);
//    const TemplateArgumentList &argList = specDecl->getTemplateInstantiationArgs();
//    std::string templateArgs = "<";
//    for (unsigned int i = 0; i < argList.size(); i++) {
//      semaRecordDecl->name =
//        llvm::outs() <<  << "\n";
//    }
//  } else {
//    semaRecordDecl->name = decl->getNameAsString();
//    semaRecordDecl->fullyQualifiedName = decl->getQualifiedNameAsString();
//  }
  for (auto *field : decl->fields()) {
    semaRecordDecl->fields.push_back(fromFieldDecl(*semaRecordDecl, field));
  }
  return semaRecordDecl;
}

std::string toSource(SemaType &type) {
  if (type.isPrimitiveType()) {
    auto kind = ((SemaPrimitiveType&) type).typeKind;
    switch (kind) {
    case BuiltinType::Kind::Float: return "float";
    case BuiltinType::Kind::Double: return "double";
    case BuiltinType::Kind::LongDouble: return "long double";
    case BuiltinType::Kind::Bool: return "bool";
    case BuiltinType::Kind::Char_S:
    case BuiltinType::Kind::SChar: return "char";
    case BuiltinType::Kind::Char_U:
    case BuiltinType::Kind::UChar: return "unsigned char";
    case BuiltinType::Kind::WChar_S:
    case BuiltinType::Kind::WChar_U: return "wchar_t";
    case BuiltinType::Kind::Short: return "short";
    case BuiltinType::Kind::UShort: return "unsigned short";
    case BuiltinType::Kind::Int: return "int";
    case BuiltinType::Kind::UInt: return "unsigned int";
    case BuiltinType::Kind::Long: return "long";
    case BuiltinType::Kind::ULong: return "unsigned long";
    case BuiltinType::Kind::LongLong: return "long long";
    case BuiltinType::Kind::ULongLong: return "unsigned long long";
    default:
      llvm::errs() << "Cannot convert primitive type to source (kind " << std::to_string(kind) << ") " << __FILE__ << ":" << __LINE__ << "\n";
    }
  }
  if (type.isEnumType()) {
    return ((SemaEnumType&) type).name;
  }
  if (type.isRecordType()) {
    return ((SemaRecordType&) type).recordDecl->name;
  }
  if (type.isConstSizeArrType()) {
    SemaConstSizeArrType *arrType = (SemaConstSizeArrType *) &type;
    std::string sizes = "[" + std::to_string(arrType->size) + "]";
    while (arrType->elementType->isConstSizeArrType()) {
      arrType = (SemaConstSizeArrType*) arrType->elementType.get();
      sizes += "[" + std::to_string(arrType->size) + "]";
    }
    sizes = toSource(*arrType->elementType) + sizes;
    return sizes;
  }
}

std::string toSource(SemaFieldDecl &decl) {
  if (decl.type->isPrimitiveType() || decl.type->isEnumType() || decl.type->isRecordType()) {
    return toSource(*decl.type) + " " + decl.name;
  }
  if (decl.type->isConstSizeArrType()) {
    SemaConstSizeArrType &arrType = (SemaConstSizeArrType&) *decl.type;
    std::string sizes;
    do {
      sizes += "[" + std::to_string(arrType.size) + "]";
    } while (arrType.elementType->isConstSizeArrType());
    return toSource(*arrType.elementType) + " " + decl.name + " " + sizes;
  }
}

#endif // CLANG_SEMAIR_H
