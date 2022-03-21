//
// Created by p on 09/03/2022.
//

#ifndef CLANG_MPISUPPORTADDER_H
#define CLANG_MPISUPPORTADDER_H

#include "../SemaIR/SemaIR.h"

struct MPISupportCode {
  std::string insideStructCode;
  std::string outsideStructCode;
};

class MPISupportAdder {

  struct MPIElement {
    std::vector<int> blockLengths;
    std::vector<std::string> types;
    std::vector<std::string> offsets;
  };

  MPIElement mapMpiElement(SemaFieldDecl &decl) {
    std::string relativeElementOffset = "offsetof(" + decl.parent->fullyQualifiedName + ", " + decl.name + ")";
    return mapMpiElement(*decl.type, relativeElementOffset);
  }

  MPIElement mapMpiElement(SemaType &type, std::string relativeElementOffset) {
    MPIElement element;

    if (type.isEnumType()) {
      return mapMpiElement(((SemaEnumType&) type).integerType, relativeElementOffset);
    }

    if (type.isPrimitiveType()) {
      element.blockLengths.push_back(1);
      element.offsets.push_back(relativeElementOffset);
      BuiltinType::Kind kind = ((SemaPrimitiveType&)type).typeKind;
      switch (kind) {
      case BuiltinType::Kind::Float:
        element.types.push_back("MPI_FLOAT");
        break;
      case BuiltinType::Kind::Double:
        element.types.push_back("MPI_DOUBLE");
        break;
      case BuiltinType::Kind::LongDouble:
        element.types.push_back("MPI_LONG_DOUBLE");
        break;
      case BuiltinType::Kind::Bool:
        element.types.push_back("MPI_BYTE");
        break;
      case BuiltinType::Kind::Char_S:
      case BuiltinType::Kind::SChar:
        element.types.push_back("MPI_CHAR");
        break;
      case BuiltinType::Kind::Char_U:
      case BuiltinType::Kind::UChar:
        element.types.push_back("MPI_UNSIGNED_CHAR");
        break;
      case BuiltinType::Kind::WChar_S:
      case BuiltinType::Kind::WChar_U:
        element.types.push_back("MPI_WCHAR");
        break;
      case BuiltinType::Kind::Short:
        element.types.push_back("MPI_SHORT");
        break;
      case BuiltinType::Kind::UShort:
        element.types.push_back("MPI_UNSIGNED_SHORT");
        break;
      case BuiltinType::Kind::Int:
        element.types.push_back("MPI_INTEGER");
        break;
      case BuiltinType::Kind::UInt:
        element.types.push_back("MPI_UNSIGNED_INTEGER");
        break;
      case BuiltinType::Kind::Long:
        element.types.push_back("MPI_LONG");
        break;
      case BuiltinType::Kind::ULong:
        element.types.push_back("MPI_UNSIGNED_LONG");
        break;
      case BuiltinType::Kind::LongLong:
        element.types.push_back("MPI_LONG_LONG");
        break;
      case BuiltinType::Kind::ULongLong:
        element.types.push_back("MPI_UNSIGNED_LONG_LONG");
        break;
      default:
        llvm::errs() << "Invalid builtin type for MPI mapping " << std::to_string(kind) << "\n";
      }
    }

    else if (type.isConstSizeArrType()) {
      SemaConstSizeArrType* constSizeArrType = (SemaConstSizeArrType*) &type;
      int size = 1;

      while (true) {
        size *= constSizeArrType->size;
        if (!constSizeArrType->elementType->isConstSizeArrType()) break;
        constSizeArrType = (SemaConstSizeArrType *) constSizeArrType->elementType.get();
      }

      auto &elementType = *constSizeArrType->elementType.get();

      if (elementType.isEnumType() || elementType.isPrimitiveType()) {

        MPIElement arrTypeElement = mapMpiElement(elementType, relativeElementOffset);
        for (auto blockLength : arrTypeElement.blockLengths) {
          element.blockLengths.push_back(blockLength * size);
        }
        for (auto typ : arrTypeElement.types) {
          element.types.push_back(typ);
        }
        for (auto offset : arrTypeElement.offsets) {
          element.offsets.push_back(offset);
        }

      } else {

        size = ((SemaConstSizeArrType*) &type)->size;

        MPIElement arrTypeElement = mapMpiElement(*((SemaConstSizeArrType&) type).elementType, "0");

        for (int i = 0; i < size; i++) {
          for (auto blockLength : arrTypeElement.blockLengths) {
            element.blockLengths.push_back(blockLength);
          }
          for (auto typ : arrTypeElement.types) {
            element.types.push_back(typ);
          }
          for (auto offset : arrTypeElement.offsets) {
            element.offsets.push_back(relativeElementOffset + " + " + "sizeof(" + toSource(elementType) + ") * " + std::to_string(i) + " + " + offset);
          }
        }

      }
    } else if (type.isRecordType()) {
      for (const auto &field : ((SemaRecordType&) type).recordDecl->fields) {
        MPIElement fieldElement = mapMpiElement(*field);
        element.blockLengths.insert(element.blockLengths.end(), fieldElement.blockLengths.begin(), fieldElement.blockLengths.end());
        element.types.insert(element.types.end(), fieldElement.types.begin(), fieldElement.types.end());
        for (std::string offset : fieldElement.offsets) {
          element.offsets.push_back(relativeElementOffset + " + " + offset);
        }
      }
    }
    return element;
  }

  std::string getInsideStructCode(std::vector<std::unique_ptr<SemaFieldDecl>> &fields) {
    SemaRecordDecl& recordDecl = *fields[0]->parent;

    std::string sourceCode = "";

    sourceCode += "static MPI_Datatype *Datatype;\n\n";

    int nitems;
    std::vector<int> blocklengths;
    std::vector<std::string> types;
    std::vector<std::string> offsets;

    for (const auto &field : recordDecl.fields) {
      MPIElement element = this->mapMpiElement(*field->type, "offsetof(" + recordDecl.name + ", " + field->name + ")");
      blocklengths.insert(blocklengths.end(), element.blockLengths.begin(), element.blockLengths.end());
      types.insert(types.end(), element.types.begin(), element.types.end());
      offsets.insert(offsets.end(), element.offsets.begin(), element.offsets.end());
    }

    nitems = blocklengths.size();

    sourceCode += "static void __initMpiDatatype() {\n"
                  "    Datatype = new MPI_Datatype;\n"
                  "    int blocklengths[" + std::to_string(nitems) + "] = { ";
    for (auto blocklength : blocklengths) {
      sourceCode += std::to_string(blocklength) + ", ";
    }
    if (blocklengths.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += " };\n\n";

    sourceCode += "    MPI_Datatype types[" + std::to_string(nitems) + "] = { ";
    for (auto type: types) {
      sourceCode += type + ", ";
    }
    if (types.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += " };\n";

    sourceCode += "    MPI_Aint offsets[" + std::to_string(nitems) + "] = {\n";
    for (auto offset : offsets) {
      sourceCode += "        " + offset + ",\n";
    }

    if (offsets.size() > 0) { // remove trailing ', '
      sourceCode.pop_back();
      sourceCode.pop_back();
    }
    sourceCode += "\n    };\n";

    sourceCode += "    MPI_Type_create_struct(" + std::to_string(nitems) + ", blocklengths, offsets, types, Datatype);\n";
    sourceCode += "    MPI_Type_commit(Datatype);\n";
    sourceCode += "}\n\n";

    sourceCode += "static MPI_Datatype getMpiDatatype() {\n"
                  "    if (!Datatype) __initMpiDatatype();\n"
                  "    return *Datatype;\n"
                  "}\n\n";

    return sourceCode;
  }

  std::string getOutsideStructCode(std::vector<std::unique_ptr<SemaFieldDecl>> &fields) {
    std::string fcqn = fields[0]->parent->fullyQualifiedName;
    return "MPI_Datatype *" + fcqn + "::Datatype = nullptr;";
  }

public:
  MPISupportCode getMPISupport(std::string structName, RecordDecl *decl) {
      auto semaRecordDecl = fromRecordDecl(decl);
      semaRecordDecl->name = structName;
      return getMPISupport(*semaRecordDecl);
  }

  MPISupportCode getMPISupport(SemaRecordDecl &decl) {
      return getMPISupport(decl.fields);
  }

  MPISupportCode getMPISupport(std::vector<std::unique_ptr<SemaFieldDecl>> &fields) {
    return {
      this->getInsideStructCode(fields),
      this->getOutsideStructCode(fields)
    };
  }

};

#endif // CLANG_MPISUPPORTADDER_H
