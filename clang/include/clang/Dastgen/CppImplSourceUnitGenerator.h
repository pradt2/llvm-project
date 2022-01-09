#include "SourceGenerator.h"
#include "CppClassGenerator.h"
#include "GeneratorNamingUtils.h"
#include "CppLanguageResolver.h"
#include "CppStatementGenerator.h"

class CppImplSourceUnitGenerator {
private:
    GeneratorNamingUtils generatorNamingUtils;
    CppLanguageResolver languageResolver;
    CppClassGenerator classGenerator;
    CppStatementGenerator statementGenerator;

    std::string include(std::string ref) {
        return "#include \"" + ref + "\"";
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

    std::vector<std::string> getConstructorDefs(DataClass *dataClass) {
        std::vector<std::string> constructorDefs;
        for (auto constructor : dataClass->constructors) {
            std::vector<std::string> constructorDef = this->classGenerator.getConstructor(
                    this->getParamsAsTuples(dataClass->parameters),
                    this->languageResolver.resolveMethodArgumentParams(constructor->methodArguments),
                    dataClass->namespaces,
                    dataClass->name,
                    this->languageResolver.resolveAccessModifier(constructor->accessModifier),
                    this->languageResolver.resolveMethodArguments(constructor->methodArguments),
                    this->statementGenerator.handleStatements(constructor->methodStatements)
            );
            constructorDefs.insert(constructorDefs.end(), constructorDef.begin(), constructorDef.end());
        }
        return constructorDefs;
    }

    std::vector<std::string> getMethodDefs(DataClass *dataClass) {
        std::vector<std::string> methodDefs;
        for (auto method : dataClass->methods) {
            std::vector<std::string> methodDef = this->classGenerator.getMethod(
                    this->getParamsAsTuples(dataClass->parameters),
                    this->languageResolver.resolveMethodArgumentParams(method->methodArguments),
                    dataClass->namespaces,
                    dataClass->name,
                    this->languageResolver.resolveAccessModifier(method->accessModifier),
                    this->languageResolver.resolveType(method->returnType),
                    method->name,
                    this->languageResolver.resolveMethodArguments(method->methodArguments),
                    this->statementGenerator.handleStatements(method->methodStatements)
            );
            methodDefs.insert(methodDefs.end(), methodDef.begin(), methodDef.end());
        }
        return methodDefs;
    }

public:
    SourceUnit *generateImplSourceUnit(DataClass *dataClass) {
        SourceUnit *unit = new SourceUnit();
        unit->name = this->generatorNamingUtils.getImplSourceUnitName(dataClass);
        unit->path = this->generatorNamingUtils.getSourceUnitPath(dataClass);

        std::string headerName;
        for (auto folderName : this->generatorNamingUtils.getSourceUnitImportPath(dataClass)) {
            headerName += folderName + "/";
        }
        headerName += this->generatorNamingUtils.getHeaderSourceUnitName(dataClass);
        unit->contents += this->include(headerName) + "\n";

        auto constructorLines = this->getConstructorDefs(dataClass);
        for (const auto& line : constructorLines) {
            unit->contents += line + "\n";
        }

        auto methodLines = this->getMethodDefs(dataClass);
        for (const auto& line : methodLines) {
            unit->contents += line + "\n";
        }

        return unit;
    }
};
