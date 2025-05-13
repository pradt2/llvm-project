//
// Created by p on 09/03/2022.
//

#ifndef CLANG_MPIMAPPINGGENERATOR_H
#define CLANG_MPIMAPPINGGENERATOR_H

#include "../SemaIR/SemaIR.h"

class MpiMappingGenerator {

  const char *resolveMpiType(const BuiltinType *type) {
    BuiltinType::Kind kind = type->getKind();
    switch (kind) {
    case BuiltinType::Kind::Float:
      return "MPI_FLOAT";
    case BuiltinType::Kind::Double:
      return "MPI_DOUBLE";
    case BuiltinType::Kind::LongDouble:
      return "MPI_LONG_DOUBLE";
    case BuiltinType::Kind::Bool:
      return "MPI_BYTE";
    case BuiltinType::Kind::Char_S:
    case BuiltinType::Kind::SChar:
      return "MPI_CHAR";
    case BuiltinType::Kind::Char_U:
    case BuiltinType::Kind::UChar:
      return "MPI_UNSIGNED_CHAR";
    case BuiltinType::Kind::WChar_S:
    case BuiltinType::Kind::WChar_U:
      return "MPI_WCHAR";
    case BuiltinType::Kind::Short:
      return "MPI_SHORT";
    case BuiltinType::Kind::UShort:
      return "MPI_UNSIGNED_SHORT";
    case BuiltinType::Kind::Int:
      return "MPI_INTEGER";
    case BuiltinType::Kind::UInt:
      return "MPI_UNSIGNED_INTEGER";
    case BuiltinType::Kind::Long:
      return "MPI_LONG";
    case BuiltinType::Kind::ULong:
      return "MPI_UNSIGNED_LONG";
    case BuiltinType::Kind::LongLong:
      return "MPI_LONG_LONG";
    case BuiltinType::Kind::ULongLong:
      return "MPI_UNSIGNED_LONG_LONG";
    default:
      assert(false && "MPI Datatype: unknown primitive type");
    }
  }

  struct MpiEntry {
    int offset;
    int blockLen;
    const char *type;
  };

  void mapMpiTypeRecursively(QualType type, int offset, std::vector<MpiEntry> &entries) {
    int blockLen = 1;

    while (type->isConstantArrayType()) {
      const ConstantArrayType *cat = cast<ConstantArrayType>(type->getAsArrayTypeUnsafe());
      blockLen *= cat->getSExtSize();
      type = cat->getElementType();
    }

    if (type->isRecordType()) {
      auto *rd = type->getAsRecordDecl();

      // this loop works around the fact that block len for complex types
      // would require building a separate MPI_Datatype for them
      for (int blockId = 0; blockId < blockLen; blockId++) {
        for (auto *field : rd->fields()) {
          auto fieldOffset = field->getParent()->getASTContext().getFieldOffset(field);
          mapMpiTypeRecursively(field->getType(), offset + fieldOffset, entries);
        }
        offset += rd->getASTContext().getTypeSize(type);
      }
    } else if (type->isBuiltinType()) {
      auto *mpiType = resolveMpiType(type->castAs<BuiltinType>());
      entries.push_back({
          offset,
          blockLen,
          mpiType});
    } else assert(false && "MPI Datatype: unknown type");
  }

  void mapMpiField(RecordDecl *baseDecl, llvm::StringRef field, std::vector<MpiEntry> &entries) {
    auto offset = 0;
    auto lastDotPos = 0;
    auto *currDecl = baseDecl;
    auto fieldType = QualType();

    while (currDecl) {
      auto nextDotPos = field.find_first_of('.', lastDotPos);
      auto fieldName = field.substr(lastDotPos, nextDotPos);

      auto *currField = (FieldDecl*) nullptr;
      for (auto *f : currDecl->fields()) if (f->getName() == fieldName)  {
        currField = f;
        break;
      }

      assert(currField && "MPI Datatype: Field not found!");
      offset += baseDecl->getASTContext().getFieldOffset(currField);

      fieldType = currField->getType();
      if (fieldType->isRecordType()) currDecl = fieldType->getAsRecordDecl();
      if (nextDotPos == -1UL) break;
      lastDotPos = nextDotPos + 1;
    }

    mapMpiTypeRecursively(fieldType, offset, entries);
  }

  std::string getMappingMethodCode(RecordDecl *baseDecl, llvm::StringRef *fieldsArgs, int fieldsSize) {
    std::string sourceCode = "";

    bool shouldOnlyIncludeExplicit = fieldsSize > 0;

    std::vector<MpiEntry> entries;

    if (shouldOnlyIncludeExplicit) {
      for (int fieldIdx = 0; fieldIdx < fieldsSize; fieldIdx++) {
        mapMpiField(baseDecl, fieldsArgs[fieldIdx], entries);
      }
    } else {
      mapMpiTypeRecursively(QualType(baseDecl->getTypeForDecl(), 0), 0, entries);
    }

    sourceCode += "    #define COMMA ,\n"
                  "    static MPI_Datatype *Datatype = nullptr;\n"
                  "    if (Datatype) return *Datatype;\n\n"
                  "    Datatype = new MPI_Datatype;\n"
                  "    int blocklengths[" + std::to_string(entries.size()) + "] = { ";
    for (auto &entry : entries) {
      sourceCode += std::to_string(entry.blockLen) + ", ";
    }
    if (entries.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += " };\n\n";

    sourceCode += "    MPI_Datatype types[" + std::to_string(entries.size()) + "] = { ";
    for (auto &entry : entries) {
      sourceCode += entry.type + std::string(", ");
    }
    if (entries.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += " };\n";

    sourceCode += "    MPI_Aint offsets[" + std::to_string(entries.size()) + "] = {\n";
    for (auto &entry : entries) {
      sourceCode += "        " + std::to_string(entry.offset / 8 /* bits to bytes */) + ",\n";
    }

    if (entries.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += "\n    };\n";

    sourceCode += "    MPI_Type_create_struct(" + std::to_string(entries.size()) + ", blocklengths, offsets, types, Datatype);\n"
                  "    MPI_Type_commit(Datatype);\n"
                  "    return *Datatype;\n"
                  "    #undef COMMA";
    return sourceCode;
  }

  MapMpiDatatypeAttr *getMapMpiDatatypeAttr(CXXMethodDecl *D) {
      if (!D->isStatic()) return nullptr;
      for (auto *attr : D->attrs()) {
          if (llvm::isa<MapMpiDatatypeAttr>(attr)) return (MapMpiDatatypeAttr*) attr;
      }
      return nullptr;
  }

public:

  bool isMpiMappingCandidate(CXXMethodDecl *D) {
    return this->getMapMpiDatatypeAttr(D) != nullptr;
  }

  std::string getMpiMappingMethodBody(CXXMethodDecl *D) {
    auto *decl = D->getParent();
    auto fieldsRef = this->getMapMpiDatatypeAttr(D)->fields_begin();
    auto code = getMappingMethodCode(decl, fieldsRef, this->getMapMpiDatatypeAttr(D)->fields_size());
    return code;
  }

};

#endif // CLANG_MPIMAPPINGGENERATOR_H
