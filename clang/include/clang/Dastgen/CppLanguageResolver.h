#include <string>
#include <iostream>
#include <tuple>
#include "DataRepresentation.h"
#include "HighLevelFeatures.h"
#include "DataClassDataTypeFinder.h"

#include "llvm/Support/Casting.h"

#ifndef __DASTGEN2_CppLanguageResolver__
#define __DASTGEN2_CppLanguageResolver__

class CppLanguageResolver : private DataClassDataTypeFinder {

public:
    std::string resolveAccessModifier(AccessModifier accessModifier) {
        switch (accessModifier) {
            case PRIVATE: return "private";
            case PROTECTED: return "protected";
            case PUBLIC: return "public";
            case ACCESS_UNKNOWN:
                break;
        }
        std::cerr << "Unknown access modifier" << std::endl;
        return "unknown_access_modifier";
    }

    std::string resolveTypeName(AggregateDataType *dataType) {
        if (llvm::isa<DataClassDataType>(dataType)) {
            return "class";
        } else if (llvm::isa<EnumDataType>(dataType)) {
            return "enum";
        } else {
            std::cerr << "Unknown aggregate data type" << std::endl;
        }
    }

    std::string resolveType(IDataType* dataType) {
        if (llvm::isa<ConstSizeArrayDataType>(dataType)) {
            return resolveType(reinterpret_cast<ConstSizeArrayDataType*>(dataType)->getItemType());
        }
        if (llvm::isa<AggregateDataType>(dataType)) {
            AggregateDataStruct* dataClass = reinterpret_cast<AggregateDataType*>(dataType)->getDataStruct();
            std::string name;
            for (const std::string& ns : dataClass->namespaces) name += ns + "::";
            name += dataClass->name;
            if (dataClass->parameters.empty()) return name;
            name += "<";
            for (auto param: dataClass->parameters) name += this->resolveDataClassParameterName(dataClass, param) + ", ";
            name.pop_back();
            name.pop_back(); //removing the trailing ', '
            name += ">";
            return name;
        }
        if (llvm::isa<ReferenceDataType>(dataType)) {
            return resolveType(reinterpret_cast<ReferenceDataType*>(dataType)->getTarget()) + "&";
        }
        if (llvm::isa<PointerDataType>(dataType)) {
            return resolveType(reinterpret_cast<PointerDataType*>(dataType)->getTarget()) + "*";
        }
        if (llvm::isa<ListDataType>(dataType)) {
            return "std::vector<" + resolveType(reinterpret_cast<ListDataType*>(dataType)->getItemType()) + ">";
        }
        if (llvm::isa<PrimitiveData>(dataType)) {
            PrimitiveData *primitiveData = reinterpret_cast<PrimitiveData*>(dataType);
            switch (primitiveData->getDataType()) {
                case VOID: return "void";
                case BOOLEAN: return "bool";
                case CHAR: return "signed char";
                case UNSIGNED_CHAR: return "unsigned char";
                case DOUBLE: return "double";
                case LONG_DOUBLE: return "long double";
                case FLOAT: return "float";
                case INT: return "signed int";
                case UNSIGNED_INT: return "unsigned int";
                case LONG: return "signed long";
                case UNSIGNED_LONG: return "unsigned long";
                case LONG_LONG: return "signed long long";
                case UNSIGNED_LONG_LONG: return "unsigned long long";
                case SHORT: return "short";
                case UNSIGNED_SHORT: return "unsigned short";
                case STRING: return "std::string";
                case UNKNOWN:
                    std::cerr << "Unknown primitive data type" << std::endl;
                    return "unknown_primitive_data_type";
            }
        }
        std::cerr << "Unknown data type" << std::endl;
        return "unknown_data_type";
    }

    std::string resolveVariableDeclarationName(IDataType *dataType, std::string name) {
        if (llvm::isa<ConstSizeArrayDataType>(dataType)) {
            ConstSizeArrayDataType *arrayDataType = reinterpret_cast<ConstSizeArrayDataType*>(dataType);
            return name + "[" + arrayDataType->getItemCount() + "]";
        }
        return name;
    }

    //this is for very specific uses only
    //namely, when a template (function, class) is declared such that
    //it references another template (function, class) declaration
    //then this function produces names for parameters for the referenced template
    //which need to be included and 'forwarded' to the referenced template
    std::string resolveDataClassParameterName(AggregateDataStruct *dataClass, DataClassParameter *parameter) {
        return dataClass->name + '_' + parameter->getName();
    }

    std::vector<std::tuple<std::string, std::string>> resolveMethodArgumentParams(const std::vector<MethodArgument*>& methodArgs) {
        std::vector<std::tuple<std::string, std::string>> params;
        std::vector<AggregateDataType *> dataTypes = this->findReferencedDataTypes(methodArgs);
        for (auto dataType : dataTypes) {
            for (auto param : dataType->getDataStruct()->parameters) {
                const std::string resolvedType = this->resolveType(param->getType());
                const std::string resolvedName = this->resolveDataClassParameterName(dataType->getDataStruct(), param);
                params.emplace_back(std::tuple<std::string, std::string> {resolvedType, resolvedName});
            }
        }
        return params;
    }

    std::vector<std::tuple<std::string, std::string>> resolveMethodArguments(const std::vector<MethodArgument*>& methodArgs) {
        std::vector<std::tuple<std::string, std::string>> arguments;
        for (auto methodArg : methodArgs) {
            const std::string resolvedType = this->resolveType(methodArg->type);
            const std::string resolveTypeInstanceName = this->resolveVariableDeclarationName(methodArg->type, methodArg->name);
            arguments.emplace_back(std::tuple<std::string, std::string> {resolvedType, resolveTypeInstanceName});
        }
        return arguments;
    }

    std::string resolveHighLevelFeatureInclude(const HighLevelFeatures feature) {
        switch (feature) {
            case HighLevelFeatures::EXCEPTION: return "stdexcept";
            case HighLevelFeatures::LIST: return "vector";
        }
    }
};

#endif
