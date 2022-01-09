#include "CppClassGenerator.h"
#include "CppLanguageResolver.h"
#include "GeneratorNamingUtils.h"
#include "SourceGenerator.h"
#include "DataClassDataTypeFinder.h"

class CppHeaderSourceUnitGenerator {
private:
    CppClassGenerator classGenerator;
    CppLanguageResolver languageResolver;
    GeneratorNamingUtils generatorNamingUtils;
    DataClassDataTypeFinder classRefFinder;

    std::vector<std::string> getIfndefBlock(DataClass *dataClass) {
        const std::string prefix = "DASTGEN2_GENERATED__";
        const std::string name = this->generatorNamingUtils.getIfndefBlockName(dataClass);
        return std::vector<std::string> {
            "#ifndef " + prefix + name,
            "#define " + prefix + name
        };
    }

    std::string include(std::string ref) {
        return "#include \"" + ref + "\"";
    }

    std::vector<std::string> getIncludes(DataClass *dataClass) {
        std::vector<std::string> includes;
        for (auto feature : dataClass->highLevelFeatures) {
            includes.emplace_back(this->include(this->languageResolver.resolveHighLevelFeatureInclude(feature)));
        }

        std::vector<AggregateDataType *> referencedClasses = this->classRefFinder.findReferencedDataTypes(dataClass);
        for (auto refClass : referencedClasses) {
            std::string headerName;
            for (auto folderName : this->generatorNamingUtils.getSourceUnitImportPath(refClass->getDataStruct())) {
                headerName += folderName + "/";
            }
            headerName += this->generatorNamingUtils.getHeaderSourceUnitName(refClass->getDataStruct());
            includes.emplace_back(this->include(headerName));
        }
        return includes;
    }

    std::vector<std::tuple<std::string, std::string>> getParamsAsTuples(std::vector<DataClassParameter *> params) {
        std::vector<std::tuple<std::string, std::string>> tuples;
        tuples.reserve(params.size());
        for (auto param : params) {
            std::string paramType = this->languageResolver.resolveType(param->getType());
            tuples.emplace_back(std::tuple<std::string, std::string>(paramType, param->getName()));
        }
        return tuples;
    }

    std::vector<std::string> getForwardDecls(DataClass *dataClass) {
        std::vector<AggregateDataType *> referencedClasses = this->classRefFinder.findReferencedDataTypes(dataClass);
        std::vector<std::string> forwardDecls;
        for (auto refClass : referencedClasses) {
            std::vector<std::string> refClassForwardDecl = this->classGenerator.getForwardDeclaration(
                    languageResolver.resolveTypeName(refClass),
                    refClass->getDataStruct()->namespaces,
                    this->getParamsAsTuples(refClass->getDataStruct()->parameters),
                    refClass->getDataStruct()->name
            );
            forwardDecls.insert(forwardDecls.end(), refClassForwardDecl.begin(), refClassForwardDecl.end());
        }
        return forwardDecls;
    }

    std::vector<std::string> getClassDecl(DataClass *dataClass) {
        std::vector<std::string> fields;
        for (auto field: dataClass->fields) {
            fields.emplace_back(this->classGenerator.getField(
                    this->languageResolver.resolveAccessModifier(field->accessModifier),
                    this->languageResolver.resolveType(field->type),
                    this->languageResolver.resolveVariableDeclarationName(field->type, field->name)
            ));
        }

        std::vector<std::string> constructors;
        for (auto constructor: dataClass->constructors) {
            const auto& constructorLines = this->classGenerator.getConstructorSignature(
                    this->languageResolver.resolveMethodArgumentParams(constructor->methodArguments),
                    this->languageResolver.resolveAccessModifier(constructor->accessModifier),
                    dataClass->name,
                    this->languageResolver.resolveMethodArguments(constructor->methodArguments)
            );
            constructors.insert(constructors.end(), constructorLines.begin(), constructorLines.end());
        }

        std::vector<std::string> methods;
        for (auto method: dataClass->methods) {
            const auto& methodLines = this->classGenerator.getMethodSignature(
                    this->languageResolver.resolveMethodArgumentParams(method->methodArguments),
                    this->languageResolver.resolveAccessModifier(method->accessModifier),
                    this->languageResolver.resolveType(method->returnType),
                    method->name,
                    this->languageResolver.resolveMethodArguments(method->methodArguments)
            );
            methods.insert(methods.end(), methodLines.begin(), methodLines.end());
        }

        std::vector<std::string> classDecl = this->classGenerator.getClassDeclaration(
                this->getParamsAsTuples(dataClass->parameters),
                dataClass->namespaces,
                dataClass->name,
                fields,
                constructors,
                methods
        );

        return classDecl;
    }

    std::vector<std::string> getIfndefEndBlock() {
        return std::vector<std::string> {"#endif"};
    }

public:
    SourceUnit *generateHeaderSourceUnit(DataClass *dataClass) {
        SourceUnit *unit = new SourceUnit();
        unit->name = this->generatorNamingUtils.getHeaderSourceUnitName(dataClass);
        unit->path = this->generatorNamingUtils.getSourceUnitPath(dataClass);

        std::vector<std::string> ifndefStartBlock = this->getIfndefBlock(dataClass);
        for (const std::string& line : ifndefStartBlock) unit->contents += line + "\n";

        std::vector<std::string> includeBlock = this->getIncludes(dataClass);
        for (const std::string& line : includeBlock) unit->contents += line + "\n";

        std::vector<std::string> forwardDeclBlock = this->getForwardDecls(dataClass);
        for (const std::string& line : forwardDeclBlock) unit->contents += line + "\n";

        std::vector<std::string> classDecl = this->getClassDecl(dataClass);
        for (const std::string& line : classDecl) unit->contents += line + "\n";

        std::vector<std::string> ifndefEndBlock = this->getIfndefEndBlock();
        for (const std::string& line : ifndefEndBlock) unit->contents += line + "\n";

        return unit;
    }
};
