//
// Created by p on 09/03/2022.
//

#ifndef CLANG_MPISUPPORTADDER_H
#define CLANG_MPISUPPORTADDER_H

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

  std::tuple<int, QualType> getArrSizeAndElemType(const ConstantArrayType *type) {
    int size = type->getSize().getSExtValue();
    while (type->getElementType()->isConstantArrayType()) {
      type = llvm::cast<ConstantArrayType>(type->getElementType().getTypePtr()->getAsArrayTypeUnsafe());
      size *= type->getSize().getSExtValue();
    }
    return std::make_tuple(size, type->getElementType());
  }

  MPIElement mapMpiElement(FieldDecl *decl) {
    std::string relativeElementOffset = "offsetof(" + decl->getParent()->getNameAsString() + ", " + decl->getNameAsString() + ")";
    return mapMpiElement(decl->getType(), relativeElementOffset);
  }

  MPIElement mapMpiElement(QualType type, std::string relativeElementOffset) {
    MPIElement element;
    auto *typePtr = type.getTypePtr();

    if (typePtr->isEnumeralType()) {
      typePtr = typePtr->castAs<EnumType>()->getDecl()->getIntegerType().getTypePtr(); // will be handled below as an int / long
    }

    if (typePtr->isBuiltinType()) {
      element.blockLengths.push_back(1);
      element.offsets.push_back(relativeElementOffset);
      auto *builtInType = typePtr->castAs<BuiltinType>();
      switch (builtInType->getKind()) {
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
      case BuiltinType::Kind::SChar:
        element.types.push_back("MPI_SIGNED_CHAR");
        break;
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
        llvm::errs() << "Invalid builtin type for MPI mapping\n";
        builtInType->dump();
      }
    }

    else if (typePtr->isConstantArrayType()) {
      auto *constArr = llvm::cast<ConstantArrayType>(typePtr->getAsArrayTypeUnsafe());

      std::tuple<int, QualType> sizeAndElementType = getArrSizeAndElemType(constArr);
      auto elementType = std::get<QualType>(sizeAndElementType);

      if (elementType.getTypePtr()->isIntegralOrEnumerationType()) {
        int size = std::get<int>(sizeAndElementType);
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
        int size = constArr->getSize().getSExtValue();

        MPIElement arrTypeElement = mapMpiElement(elementType, "0");
        for (int i = 0; i < size; i++) {
          for (auto blockLength : arrTypeElement.blockLengths) {
            element.blockLengths.push_back(blockLength);
          }
          for (auto typ : arrTypeElement.types) {
            element.types.push_back(typ);
          }
          for (auto offset : arrTypeElement.offsets) {
            element.offsets.push_back(relativeElementOffset + " + " + "sizeof(" + elementType.getAsString() + ") * " + std::to_string(i) + " + " + offset);
          }
        }
      }
    } else if (typePtr->isRecordType()) {
      for (auto *field : typePtr->castAs<RecordType>()->getDecl()->fields()) {
        MPIElement fieldElement = mapMpiElement(field);
        element.blockLengths.insert(element.blockLengths.end(), fieldElement.blockLengths.begin(), fieldElement.blockLengths.end());
        element.types.insert(element.types.end(), fieldElement.types.begin(), fieldElement.types.end());
        for (std::string offset : fieldElement.offsets) {
          element.offsets.push_back(relativeElementOffset + " + " + offset);
        }
      }
    }
    return element;
  }

  std::string getInsideStructCode(std::string structName, RecordDecl *decl) {
    std::string sourceCode = "";

    sourceCode += "int _senderDestinationRank;\n\n";

    sourceCode += "static MPI_Datatype Datatype;\n\n";

    sourceCode += "int getSenderRank() const {\n"
                  "   return _senderDestinationRank;\n"
                  "}\n\n";

    sourceCode += "static void send(const " + structName + " &buffer, int destination, int tag, MPI_Comm communicator) {\n"
                  "    MPI_Send(&buffer, 1, Datatype, destination, tag, communicator);\n"
                  "}\n\n";

    sourceCode += "static void receive(" + structName + " &buffer, int source, int tag, MPI_Comm communicator ) {\n"
                  "    MPI_Status status;\n"
                  "    MPI_Recv( &buffer, 1, Datatype, source, tag, communicator, &status);\n"
                  "    buffer._senderDestinationRank = status.MPI_SOURCE;\n"
                  "}\n\n";

    sourceCode += "static void send(const " + structName + "&buffer, int destination, int tag, std::function<void()> waitFunctor, MPI_Comm communicator ) {\n"
                  "    MPI_Request sendRequestHandle;\n"
                  "    int flag = 0;\n"
                  "    MPI_Isend( &buffer, 1, Datatype, destination, tag, communicator, &sendRequestHandle );\n"
                  "    MPI_Test( &sendRequestHandle, &flag, MPI_STATUS_IGNORE );\n"
                  "    while (!flag) {\n"
                  "        waitFunctor();\n"
                  "        MPI_Test( &sendRequestHandle, &flag, MPI_STATUS_IGNORE );\n"
                  "    }\n"
                  "}\n\n";

    sourceCode += "static void receive(" + structName + " &buffer, int source, int tag, std::function<void()> waitFunctor, MPI_Comm communicator ) {\n"
                  "    MPI_Status  status;\n"
                  "    MPI_Request receiveRequestHandle;\n"
                  "    int flag = 0;\n"
                  "    MPI_Irecv( &buffer, 1, Datatype, source, tag, communicator, &receiveRequestHandle );\n"
                  "    MPI_Test( &receiveRequestHandle, &flag, &status );\n"
                  "    while (!flag) {\n"
                  "        waitFunctor();\n"
                  "        MPI_Test( &receiveRequestHandle, &flag, &status );\n"
                  "    }\n"
                  "    buffer._senderDestinationRank = status.MPI_SOURCE;\n"
                  "}\n\n";

    sourceCode += "static void shutdownDatatype() {\n"
                  "    MPI_Type_free( &Datatype );\n"
                  "}\n\n";

    int nitems;
    std::vector<int> blocklengths;
    std::vector<std::string> types;
    std::vector<std::string> offsets;

    for (auto *field : decl->fields()) {
      MPIElement element = this->mapMpiElement(field->getType(), "offsetof(" + structName + ", " + field->getNameAsString() + ")");
      blocklengths.insert(blocklengths.end(), element.blockLengths.begin(), element.blockLengths.end());
      types.insert(types.end(), element.types.begin(), element.types.end());
      offsets.insert(offsets.end(), element.offsets.begin(), element.offsets.end());
    }

    nitems = blocklengths.size();

    sourceCode += "static void initDatatype() {\n"
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
    sourceCode += " };\n";

    sourceCode += "    MPI_Type_create_struct(" + std::to_string(nitems) + ", blocklengths, offsets, types, &Datatype);\n";
    sourceCode += "    MPI_Type_commit(&Datatype);\n";
    sourceCode += "}\n";
    return sourceCode;
  }

  std::string getOutsideStructCode(std::string structFqcn) {
    return "MPI_Datatype " + structFqcn + "::Datatype;\n";
  }

public:
  MPISupportCode getMPISupport(std::string structName, RecordDecl *decl) {
    return {
      this->getInsideStructCode(structName, decl),
      this->getOutsideStructCode(structName)
    };
  }

};

#endif // CLANG_MPISUPPORTADDER_H
