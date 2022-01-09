#ifndef __DASTGEN2_CppClassGenerator__
#define __DASTGEN2_CppClassGenerator__

#include <vector>
#include <tuple>
#include "NestedBlockGenerator.h"

class CppClassGenerator : private NestedBlockGenerator {

    std::string getComment(const std::string& comment) {
        return "// " + comment + "\n";
    }

    std::vector<std::string> getComment(const std::vector<std::string>& comment) {
        std::vector<std::string> lines;
        lines.reserve(comment.size());
        for (const auto& line : comment) {
            lines.push_back(this->getComment(line));
        }
        return lines;
    }

    std::string getParametersDeclaration(const std::vector<std::tuple<std::string, std::string>>& parameters) {
        if (parameters.size() == 0) return "";
        std::string parametersStr = "template<";
        for (std::tuple<std::string, std::string> parameter : parameters) {
            parametersStr += std::get<0>(parameter) + " " + std::get<1>(parameter) + ", ";
        }
        parametersStr.pop_back();
        parametersStr.pop_back(); //removing the trailing ', '
        parametersStr += ">";
        return parametersStr;
    }

    std::string getParametersForwardReference(const std::vector<std::tuple<std::string, std::string>>& parameters) {
        if (parameters.size() == 0) return "";
        std::string parametersStr = "<";
        for (std::tuple<std::string, std::string> parameter : parameters) {
            parametersStr += std::get<1>(parameter) + ", ";
        }
        parametersStr.pop_back();
        parametersStr.pop_back(); //removing the trailing ', '
        parametersStr += ">";
        return parametersStr;
    }

    std::string getMethodNameAndArgs(const std::string& name,
                                     const std::vector<std::tuple<std::string, std::string>>& args) {
        std::string methodSignature = name + "(";
        if (!args.empty()) {
            for (auto arg: args) {
                methodSignature += std::get<0>(arg) + " " + std::get<1>(arg) + ", ";
            }
            methodSignature.pop_back();
            methodSignature.pop_back(); //removing the last redundant ", "
        }
        methodSignature += ")";
        return methodSignature;
    }

public:

    std::string getField(const std::string& accessModifier, const std::string& type, const std::string& name) {
        return accessModifier + ": " + type + " " + name + ";";
    }

    std::vector<std::string> getMethodSignature(const std::vector<std::tuple<std::string, std::string>>& params,
                                               const std::string& accessModifier,
                                               const std::string& type,
                                               const std::string& name,
                                               const std::vector<std::tuple<std::string, std::string>>& args) {
        std::string paramsDecl = this->getParametersDeclaration(params);
        std::string nameAndArgs = this->getMethodNameAndArgs(name, args);
        return std::vector<std::string> {
            accessModifier + ": " + paramsDecl + " " + type + " " + nameAndArgs + ";"
        };
    }

    std::vector<std::string> getMethod(const std::vector<std::tuple<std::string, std::string>>& classParams,
                                       const std::vector<std::tuple<std::string, std::string>>& methodArgParams,
                                       const std::vector<std::string>& namespaces,
                                       const std::string& className,
                                       const std::string& accessModifier,
                                       const std::string& type,
                                       const std::string& name,
                                       const std::vector<std::tuple<std::string, std::string>>& args,
                                       const std::vector<std::string>& bodyLines) {
        std::string classParamsStr = this->getParametersDeclaration(classParams);
        std::string methodParamsStr = this->getParametersDeclaration(methodArgParams);
        std::string methodSignature = type + " ";
        for (const auto& ns : namespaces) {
            methodSignature += ns + "::";
        }
        std::string paramsForwardRefString = this->getParametersForwardReference(classParams);
        methodSignature += className + paramsForwardRefString + "::" + this->getMethodNameAndArgs(name, args);
        return this->getIndentedBlock(std::vector<std::string>{classParamsStr, methodParamsStr, methodSignature}, bodyLines, 0);
    }

    std::vector<std::string> getConstructorSignature(
                                        const std::vector<std::tuple<std::string, std::string>>& params,
                                        const std::string& accessModifier,
                                        const std::string& parentClassName,
                                        const std::vector<std::tuple<std::string, std::string>>& args) {
        return this->getMethodSignature(params, accessModifier, "", parentClassName, args);
    }

    std::vector<std::string> getConstructor(const std::vector<std::tuple<std::string, std::string>>& classParams,
                                            const std::vector<std::tuple<std::string, std::string>>& methodArgParams,
                                           const std::vector<std::string> namespaces,
                                           const std::string& className,
                                           const std::string& accessModifier,
                                           const std::vector<std::tuple<std::string, std::string>>& args,
                                           const std::vector<std::string>& bodyLines) {
        std::string classParamsStr = this->getParametersDeclaration(classParams);
        std::string methodParamsStr = this->getParametersDeclaration(methodArgParams);
        std::string methodSignature;
        for (const auto& ns : namespaces) {
            methodSignature += ns + "::";
        }
        std::string paramsForwardRefString = this->getParametersForwardReference(classParams);
        methodSignature += className + paramsForwardRefString + "::" + this->getMethodNameAndArgs(className, args);
        return this->getIndentedBlock(std::vector<std::string>{classParamsStr, methodParamsStr, methodSignature}, bodyLines, 0);
    }

    std::vector<std::string> getForwardDeclaration(const std::string type,
                                                   const std::vector<std::string> namespaces,
                                                   const std::vector<std::tuple<std::string, std::string>>& parameters,
                                                   const std::string& className) {
        std::vector<std::string> output = std::vector<std::string> {this->getParametersDeclaration(parameters), type + " " + className + ";"};
        for (auto i = namespaces.size() - 1; i < namespaces.size(); i--) {
            output = this->getIndentedBlock("namespace " + namespaces.at(i), output, 0);
        }
        return output;
    }

    std::vector<std::string> getClassDeclaration(const std::vector<std::tuple<std::string, std::string>>& params,
                                                 const std::vector<std::string> namespaces,
                                                 const std::string& className,
                                                 const std::vector<std::string>& fields,
                                                 const std::vector<std::string>& constructorDeclarations,
                                                 const std::vector<std::string>& methodDeclarations) {
        std::vector<std::string> output;
        std::vector<std::string> fieldsAndMethods;
        fieldsAndMethods.insert(fieldsAndMethods.end(), fields.begin(), fields.end());
        fieldsAndMethods.insert(fieldsAndMethods.end(), constructorDeclarations.begin(), constructorDeclarations.end());
        fieldsAndMethods.insert(fieldsAndMethods.end(), methodDeclarations.begin(), methodDeclarations.end());
        std::string paramsString = this->getParametersDeclaration(params);
        output = this->getIndentedBlock(std::vector<std::string> {paramsString, "class " + className}, fieldsAndMethods, 0);

        for (auto i = namespaces.size() - 1; i < namespaces.size(); i--) {
            output = this->getIndentedBlock("namespace " + namespaces.at(i), output, 0);
        }

        return output;
    }

};

#endif
