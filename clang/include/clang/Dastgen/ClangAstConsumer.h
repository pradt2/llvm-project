#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "DataRepresentation.h"
#include <regex>

#ifndef __DASTGEN2_ClangAstConsumer__
#define __DASTGEN2_ClangAstConsumer__

class ClangAstConsumer :
        public clang::ASTConsumer,
        public clang::RecursiveASTVisitor<ClangAstConsumer> {

private:
    std::vector<DataClass *> *classes;
    std::vector<Enum *> *enums;

    std::vector<std::string> split(std::string s, std::string delimiter) {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
            token = s.substr (pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back (token);
        }

        res.push_back (s.substr (pos_start));
        return res;
    }

    AccessModifier getAccessModifier(clang::AccessSpecifier specifier) {
        switch (specifier) {
            case clang::AS_private: return AccessModifier::PRIVATE;
            case clang::AS_protected: return AccessModifier::PROTECTED;
            case clang::AS_public: return AccessModifier::PUBLIC;
            default: return AccessModifier::ACCESS_UNKNOWN;
        }
    }

    PrimitiveData *getPrimitiveDataType(const clang::Type *fieldType) {
        const clang::BuiltinType *fieldTypeActual = (clang::BuiltinType *) fieldType;
        clang::BuiltinType::Kind fieldKind = fieldTypeActual->getKind();
        switch (fieldKind) {
            case clang::BuiltinType::Bool: return new PrimitiveData(BOOLEAN);
            case clang::BuiltinType::SChar: return new PrimitiveData(CHAR);
            case clang::BuiltinType::UChar: return new PrimitiveData(UNSIGNED_CHAR);
            case clang::BuiltinType::Char_S: return new PrimitiveData(CHAR);
            case clang::BuiltinType::Char_U: return new PrimitiveData(UNSIGNED_CHAR);
            case clang::BuiltinType::Double: return new PrimitiveData(DOUBLE);
            case clang::BuiltinType::LongDouble: return new PrimitiveData(LONG_DOUBLE);
            case clang::BuiltinType::Float: return new PrimitiveData(FLOAT);
            case clang::BuiltinType::Int: return new PrimitiveData(INT);
            case clang::BuiltinType::UInt: return new PrimitiveData(UNSIGNED_INT);
            case clang::BuiltinType::Long: return new PrimitiveData(LONG);
            case clang::BuiltinType::ULong: return new PrimitiveData(UNSIGNED_LONG);
            case clang::BuiltinType::LongLong: return new PrimitiveData(LONG_LONG);
            case clang::BuiltinType::ULongLong: return new PrimitiveData(UNSIGNED_LONG_LONG);
            case clang::BuiltinType::Short: return new PrimitiveData(SHORT);
            case clang::BuiltinType::UShort: return new PrimitiveData(UNSIGNED_SHORT);
            default:
                std::cerr << "Unknown primitive data type" << std::endl;
                return new PrimitiveData(UNKNOWN);
        }
    }

    std::string getEnumName(clang::EnumDecl *ed) {
        return ed->getNameAsString();
    }

    std::string getDataClassName(clang::CXXRecordDecl *rd) {
        return rd->getNameAsString();
    }

    EnumDataType *getEnumDataType(const clang::Type *fieldType) {
        const clang::EnumType *fieldTypeActual = (clang::EnumType *) fieldType;
        std::string enumName = this->getEnumName(fieldTypeActual->getDecl());
        Enum *anEnum = nullptr;
        for (auto knownEnum : *this->enums) {
            if (knownEnum->name == enumName) {
                anEnum = knownEnum;
                break;
            }
        }
        if (anEnum == nullptr) {
            std::cerr << "Enum declaration not found!" << std::endl;
        }
        EnumDataType *enumDataType = new EnumDataType(anEnum);
        return enumDataType;
    }

    ConstSizeArrayDataType *getConstSizeArrDataType(const clang::Type *fieldType) {
        const clang::ConstantArrayType *fieldTypeActual = (clang::ConstantArrayType *) fieldType;
        IDataType *itemType = this->getDataType(fieldTypeActual->getElementType());
        std::string size = std::to_string(fieldTypeActual->getSize().getSExtValue());
        ConstSizeArrayDataType *dataType = new ConstSizeArrayDataType(itemType, size);
        return dataType;
    }

    DataClassDataType *getDataClassDataType(const clang::Type *fieldType) {
        const clang::RecordType *fieldTypeActual = fieldType->getAs<clang::RecordType>();
        const std::string dataClassName = this->getDataClassName((clang::CXXRecordDecl*) fieldTypeActual->getDecl());
        DataClass *dataClass = nullptr;
        for (auto knownDataClasses : *this->classes) {
            if (knownDataClasses->name == dataClassName) {
                dataClass = knownDataClasses;
                break;
            }
        }
        if (dataClass == nullptr) {
            std::cerr << "Data class declaration not found!" << std::endl;
        }
        DataClassDataType *dataClassDataType = new DataClassDataType(dataClass);
        return dataClassDataType;
    }

    ReferenceDataType *getReferenceType(const clang::Type *fieldType) {
        const clang::ReferenceType *fieldTypeActual = fieldType->getAs<clang::ReferenceType>();
        return new ReferenceDataType(this->getDataType(fieldTypeActual->getPointeeType()));
    }

    PointerDataType *getPointerType(const clang::Type *fieldType) {
        const clang::PointerType *fieldTypeActual = fieldType->getAs<clang::PointerType>();
        return new PointerDataType(this->getDataType(fieldTypeActual->getPointeeType()));
    }

    IDataType *getDataType(clang::QualType qualType) {
        const clang::Type *fieldType = qualType.getTypePtr()->getUnqualifiedDesugaredType();
        if (fieldType->isBuiltinType()) return getPrimitiveDataType(fieldType);
        if (fieldType->isEnumeralType()) return getEnumDataType(fieldType);
        if (fieldType->isConstantArrayType()) return getConstSizeArrDataType(fieldType);
        if (this->isStringType(fieldType)) return new PrimitiveData(PrimitiveDataType::STRING);
        if (fieldType->isClassType()) return getDataClassDataType(fieldType);
        if (fieldType->isReferenceType()) return getReferenceType(fieldType);
        if (fieldType->isPointerType()) return getPointerType(fieldType);
        std::cerr << "Unknown data type" << std::endl;
        return new PrimitiveData(PrimitiveDataType::UNKNOWN);
    }

    bool isStringType(const clang::Type *fieldType) {
        const clang::RecordType *fieldTypeActual = fieldType->getAs<clang::RecordType>();
        if (fieldTypeActual == nullptr) return false;
        const std::string dataClassName = this->getDataClassName((clang::CXXRecordDecl*) fieldTypeActual->getDecl());
        if (dataClassName == "basic_string") return true;
        return false;
    }

    std::vector<clang::Attr *> getDirectChildrenOfKind(clang::Decl *d, clang::attr::Kind kind) {
        std::vector<clang::Attr *> attrs;
        for (auto item : d->attrs()) {
            if (item->getKind() != kind) continue;
            attrs.push_back(item);
        }
        return attrs;
    }

    std::vector<clang::Decl *> getDirectChildrenOfKind(clang::RecordDecl *d, clang::Decl::Kind kind) {
        std::vector<clang::Decl *> attrs;
        for (auto item : d->decls()) {
            if (item->getKind() != kind) continue;
            attrs.push_back(item);
        }
        return attrs;
    }

    std::vector<Annotation *> getAnnotations(clang::Decl *d, AnnotatedType *parent) {
        std::vector<Annotation *> annotations;
        std::vector<clang::Attr *> astAnnotations = this->getDirectChildrenOfKind(d, clang::attr::Annotate);
        for (auto astAnnotation : astAnnotations) {
            Annotation *annotation = new Annotation();
            clang::AnnotateAttr *typecastAstAnnotation = static_cast<clang::AnnotateAttr *>(astAnnotation);
            annotation->tokens = split(std::string(typecastAstAnnotation->getAnnotation()), " ");
            annotation->parentField = parent;
            annotations.push_back(annotation);
        }
        return annotations;
    }

    std::vector<ClassField *> getClassFields(clang::CXXRecordDecl *rd, DataClass *parent) {
        std::vector<ClassField *> classFields;
        auto astClassFields = this->getDirectChildrenOfKind(rd, clang::Decl::Field);
        for (auto astClassField : astClassFields) {
            ClassField *classField = new ClassField();
            clang::FieldDecl *typecastAstClassField = static_cast<clang::FieldDecl *>(astClassField);
            classField->parentClass = parent;
            classField->annotations = this->getAnnotations(typecastAstClassField, classField);
            classField->name = std::string(typecastAstClassField->getName());
            classField->type = this->getDataType(typecastAstClassField->getType());
            classField->accessModifier = getAccessModifier(typecastAstClassField->getAccess());
            classField->decl = typecastAstClassField;
            classFields.push_back(classField);
        }
        return classFields;
    }

    std::vector<std::string> getNamespaces(std::string fullName) {
        std::vector<std::string> namespaces;
        std::regex e ("::");
        std::regex_token_iterator<std::string::iterator> rend;
        std::regex_token_iterator<std::string::iterator> regIt ( fullName.begin(), fullName.end(), e, -1 );
        while (regIt!=rend){
            namespaces.push_back(regIt->str());
            regIt++;
        }
        namespaces.pop_back();
        return namespaces;
    }

    DataClass *getClass(clang::CXXRecordDecl *rd) {
        DataClass *dataClass = new DataClass();
        dataClass->name = rd->getName().str();
        dataClass->decl = rd;
        dataClass->namespaces = this->getNamespaces(rd->getQualifiedNameAsString());
        dataClass->annotations = this->getAnnotations(rd, dataClass);
        dataClass->fields = this->getClassFields(rd, dataClass);
        clang::SourceManager &srcMgr = rd->getASTContext().getSourceManager();
        const clang::FileEntry *fileEntry = srcMgr.getFileEntryForID(srcMgr.getFileID(rd->getBeginLoc()));
        if (fileEntry != nullptr) {
            dataClass->originalSourceUnitPath = fileEntry->tryGetRealPathName().str();
        }
        for (auto baseClass : rd->bases()) {
            const clang::RecordType *fieldTypeActual = baseClass.getType()->getAs<clang::RecordType>();
            clang::CXXRecordDecl *baseRecord = (clang::CXXRecordDecl*) fieldTypeActual->getDecl();
            DataClass *baseDataClass = this->getClass(baseRecord);
            dataClass->annotations.insert(dataClass->annotations.begin(), baseDataClass->annotations.begin(), baseDataClass->annotations.end());
            dataClass->fields.insert(dataClass->fields.begin(), baseDataClass->fields.begin(), baseDataClass->fields.end());
        }
        return dataClass;
    }

    Enum *getEnum(clang::EnumDecl *ed) {
        Enum *anEnum = new Enum();
        anEnum->name = this->getEnumName(ed);
        anEnum->decl = ed;

        anEnum->namespaces = this->getNamespaces(ed->getQualifiedNameAsString());
        for (auto it = ed->enumerator_begin(); it != ed->enumerator_end(); ++it) {
            const std::string name = it->getNameAsString();
            const int value = it->getInitVal().getExtValue();
            anEnum->values.emplace_back(EnumValue(name, value));
        }
        clang::SourceManager &srcMgr = ed->getASTContext().getSourceManager();
        const clang::FileEntry *fileEntry = srcMgr.getFileEntryForID(srcMgr.getFileID(ed->getBeginLoc()));
        if (fileEntry != nullptr) {
            anEnum->originalSourceUnitPath = fileEntry->tryGetRealPathName().str();
        }
        return anEnum;
    }

    bool isVisitableDecl(clang::TagDecl *rd) {
        bool isEmpty = !rd->isCompleteDefinition();
        if (isEmpty) return false;
        bool isTemplated = rd->isTemplated();
        if (isTemplated) return false;
        clang::SourceManager &srcMgr = rd->getASTContext().getSourceManager();
        const clang::FileEntry *fileEntry = srcMgr.getFileEntryForID(srcMgr.getFileID(rd->getBeginLoc()));
        if (fileEntry == nullptr) return false;
        std::string path = fileEntry->tryGetRealPathName().str();
        if (path.empty() || path.rfind("/usr") == 0) return false;
        return true;
    }

public:
    ClangAstConsumer(std::vector<DataClass *> *dataClasses,
                     std::vector<Enum *> *dataEnums) :
         classes(dataClasses),
         enums(dataEnums) {}

    void HandleTranslationUnit(clang::ASTContext &Context) override {
        clang::TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
        this->TraverseDecl(decl);
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *fd) {
        if (!this->isVisitableDecl(fd)) return true;
        DataClass *dataClass = this->getClass(fd);
        this->classes->push_back(dataClass);
        return true;
    }

    bool VisitEnumDecl(clang::EnumDecl *ed) {
        if (!this->isVisitableDecl(ed)) return true;
        Enum *anEnum = this->getEnum(ed);
        this->enums->push_back(anEnum);
        return true;
    }

    bool shouldVisitTemplateInstantiations() const {
        return true;
    }

};

#endif
