#include <utility>

#include "DataRepresentation.h"

#ifndef __DASTGEN2_GetterSetterGenerator__
#define __DASTGEN2_GetterSetterGenerator__

class GetterSetterGenerator {
private:
    const std::string setterValArgName = "val";

    std::string getGetterName(std::string variableName) {
        variableName[0] = std::toupper(variableName[0]);
        return "get" + variableName;
    }

    bool isGetter(ClassMethod *classMethod) {
        bool isGetter = classMethod->name.size() >= 3 && classMethod->name.substr(0, 3) == std::string("get");
        return isGetter;
    }

    std::string getGetterVariableName(std::string getterName) {
        getterName.erase(0, 3);
        getterName[0] = std::tolower(getterName[0]);
        return getterName;
    }

    bool isSetter(ClassMethod *classMethod) {
        bool isSetter = classMethod->name.size() >= 3 && classMethod->name.substr(0, 3) == std::string("set");
        return isSetter;
    }

    std::string getSetterName(std::string variableName) {
        variableName[0] = std::toupper(variableName[0]);
        return "set" + variableName;
    }

    std::string getSetterVariableName(std::string setterName) {
        return getGetterVariableName(setterName);
    }

    IStatement *getGetterStatement(std::string variableName) {
        return new ReturnStatement(new ClassFieldVariableAccess(new LocalVariableAccess(std::move(variableName))));
    }

    IStatement *getSetterStatement(std::string variableName) {
        auto fieldRef = new ClassFieldVariableAccess(new LocalVariableAccess(std::move(variableName)));
        return new ValueAssignment(fieldRef, new LocalVariableAccess(setterValArgName));
    }

    ClassMethod *generateGetter(IDataType *dataType, const std::string& classFieldName) {
        ClassMethod *getterMethod = new ClassMethod();
        getterMethod->accessModifier = AccessModifier::PUBLIC;
        getterMethod->returnType = dataType;
        getterMethod->name = getGetterName(classFieldName);
        getterMethod->methodStatements.emplace_back(getGetterStatement(classFieldName));
        return getterMethod;
    }

    ClassMethod *generateSetter(IDataType *dataType, const std::string& classFieldName) {
        ClassMethod *setterMethod = new ClassMethod();
        setterMethod->accessModifier = AccessModifier::PUBLIC;
        setterMethod->returnType = new PrimitiveData(PrimitiveDataType::VOID);
        setterMethod->name = getSetterName(classFieldName);
        setterMethod->methodStatements.emplace_back(getSetterStatement(classFieldName));
        MethodArgument *setterArg = new MethodArgument();
        setterArg->type = dataType;
        setterArg->name = std::string(setterValArgName);
        setterMethod->methodArguments.emplace_back(setterArg);
        return setterMethod;
    }

    ClassMethod *getGetterFor(DataClass *dataClass, std::string variableName) {
        std::string getterName = getGetterName(std::move(variableName));
        for (auto method : dataClass->methods) {
            if (method->name == getterName) {
                return method;
            }
        }
        return nullptr;
    }

    ClassMethod *getSetterFor(DataClass *dataClass, std::string variableName) {
        std::string setterName = getSetterName(std::move(variableName));
        for (auto method : dataClass->methods) {
            if (method->name == setterName) {
                return method;
            }
        }
        return nullptr;
    }

public:
    DataClass *generateGettersSetters(DataClass *sourceClass) {
        for (auto field : sourceClass->fields) {
            sourceClass->methods.emplace_back(generateGetter(field));
            sourceClass->methods.emplace_back(generateSetter(field));
        }
        return sourceClass;
    }

    ClassMethod *generateGetter(ClassField *classField) {
        ClassMethod *getter = generateGetter(classField->type, classField->name);
        return getter;
    }

    ClassMethod *generateSetter(ClassField *classField) {
        ClassMethod *setter = generateSetter(classField->type, classField->name);
        return setter;
    }

    ClassMethod *getVariableGetter(ClassField *classField) {
        ClassMethod *getter = getGetterFor(classField->parentClass, classField->name);
        return getter;
    }

    ClassMethod *getVariableSetter(ClassField *classField) {
        ClassMethod *setter = getSetterFor(classField->parentClass, classField->name);
        return setter;
    }

    std::vector<ClassMethod *> getGetters(DataClass *dataClass) {
        std::vector<ClassMethod *> getters;
        for (auto method: dataClass->methods) {
            if (isGetter(method)) getters.emplace_back(method);
        }
        return getters;
    }

    std::vector<ClassMethod *> getSetters(DataClass *dataClass) {
        std::vector<ClassMethod *> setters;
        for (auto method: dataClass->methods) {
            if (isSetter(method)) setters.emplace_back(method);
        }
        return setters;
    }

    ClassMethod *getCorrespondingGetter(ClassMethod *setter, DataClass *dataClass) {
        std::string variableName = getSetterVariableName(setter->name);
        ClassMethod *getter = getGetterFor(dataClass, variableName);
        return getter;
    }
};

#endif