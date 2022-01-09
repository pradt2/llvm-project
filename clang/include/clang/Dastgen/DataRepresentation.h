#include <string>
#include <utility>
#include <vector>
#include <set>
#include "clang/AST/AST.h"
#include "LogicRepresentation.h"
#include "DataType.h"

#ifndef __DASTGEN2_DataRepresentation__
#define __DASTGEN2_DataRepresentation__

enum AccessModifier {
    PRIVATE,
    PROTECTED,
    PUBLIC,
    ACCESS_UNKNOWN
};

class DataClass;
class ClassField;
class AnnotatedType {
public:
    enum Kind {
        Class,
        Field,
        Method
    };
    Kind kind;
    AnnotatedType(Kind kind1): kind(kind1) {}
};

class Annotation {
public:
    std::vector<std::string> tokens;
    AnnotatedType *parentField;
    Annotation *copy() {
        auto *annotation = new Annotation();
        annotation->parentField = parentField;
        for (const auto& token : tokens) {
            annotation->tokens.emplace_back(std::string(token));
        }
        return annotation;
    }
};

class ClassField : public AnnotatedType {
public:
    ClassField() : AnnotatedType(AnnotatedType::Kind::Field) {}
    AccessModifier accessModifier;
    IDataType *type;
    std::string name;
    std::vector<Annotation*> annotations;
    DataClass *parentClass;
    clang::FieldDecl *decl;
    ClassField *copy() {
        ClassField *classField = new ClassField();
        classField->accessModifier = accessModifier;
        classField->type = type->copy();
        classField->name = std::string(name);
        for (auto annotation : annotations) {
            classField->annotations.emplace_back(annotation->copy());
        }
        classField->parentClass = parentClass;
        classField->decl = decl;
        return classField;
    }
};

class MethodArgument {
public:
    IDataType* type;
    std::string name;
};

class ClassMethod : public AnnotatedType {
public:
    ClassMethod() : AnnotatedType(AnnotatedType::Kind::Method) {}
    AccessModifier accessModifier;
    IDataType *returnType;
    std::string name;
    clang::CXXMethodDecl *decl;
    std::vector<MethodArgument*> methodArguments;
    std::vector<IStatement*> methodStatements;
    std::vector<Annotation*> annotations;
};

class ClassConstructor {
public:
    ClassConstructor() {}
    DataClass *parentClass;
    clang::CXXMethodDecl *decl;
    AccessModifier accessModifier = AccessModifier::PUBLIC;
    std::vector<MethodArgument*> methodArguments;
    std::vector<IStatement*> methodStatements;
};

class DataClassParameter {
private:
    IDataType* _type;
    std::string _name;
public:
    DataClassParameter(PrimitiveDataType type, std::string name) :
            _type(new PrimitiveData(type)),
            _name(std::move(name)) {}

    IDataType *getType() {
        return _type;
    }

    std::string getName() {
        return this->_name;
    }
};

class AggregateDataStruct {
public:
    std::vector<std::string> namespaces;
    std::string name;
    std::string originalSourceUnitPath;
    std::vector<DataClassParameter*> parameters;
};

class DataClass : public AggregateDataStruct, public AnnotatedType {
public:
    DataClass() : AnnotatedType(AnnotatedType::Kind::Class) {}
    clang::CXXRecordDecl *decl;
    std::set<HighLevelFeatures> highLevelFeatures;
    std::vector<DataClass*> superClasses;
    std::vector<ClassField*> fields;
    std::vector<ClassConstructor*> constructors;
    std::vector<ClassMethod*> methods;
    std::vector<Annotation*> annotations;
};

class EnumValue {
private:
    std::string _name;
    int _value;
public:
    EnumValue(std::string name, unsigned int value) :
        _name(std::move(name)),
        _value(value) {}

    std::string getName() {
        return this->_name;
    }

    unsigned int getValue() {
        return this->_value;
    }
};

class Enum : public AggregateDataStruct {
public:
    clang::EnumDecl *decl;
    std::vector<EnumValue> values;
};

class AggregateDataType : public IDataType {
private:
    AggregateDataStruct *_dataStruct;
public:
    explicit AggregateDataType(AggregateDataStruct *dataStruct) :
        _dataStruct(dataStruct) {}

    AggregateDataStruct *getDataStruct() {
        return this->_dataStruct;
    }
};

class DataClassDataType : public AggregateDataType {
public:
    explicit DataClassDataType(DataClass *dataStruct) :
        AggregateDataType(dataStruct) {}

    DataClass *getDataClass() {
        return static_cast<DataClass *>(this->getDataStruct());
    }
};

class EnumDataType : public AggregateDataType {
public:
    explicit EnumDataType(Enum *anEnum) :
    AggregateDataType(anEnum) {}

    Enum *getTargetEnum() {
        return static_cast<Enum *>(this->getDataStruct());
    }
};

#endif
