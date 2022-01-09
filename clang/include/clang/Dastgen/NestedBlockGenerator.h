#include <string>
#include <vector>

#ifndef __DASTGEN2_NestedBlockGenerator__
#define __DASTGEN2_NestedBlockGenerator__

class NestedBlockGenerator {
protected:
    std::string indent_template = "    ";

    std::string getIndent(int indent) {
        std::string output;
        for (int i = 0; i < indent; i++) {
            output += this->indent_template;
        }
        return output;
    }

    std::string getIndentedLine(int indent, const std::string& line) {
        return this->getIndent(indent) + line + "\n";
    }

    std::vector<std::string> getIndentedBlock(const std::string& header, const std::vector<std::string>& bodyLines, int indent) {
        return this->getIndentedBlock(std::vector<std::string> {header}, bodyLines, indent);
    }

    std::vector<std::string> getIndentedBlock(const std::vector<std::string>& header, const std::vector<std::string>& bodyLines, int indent) {
        std::vector<std::string> output;
        output.insert(output.end(), header.begin(), header.end()-1);
        output.push_back(this->getIndentedLine(indent, header.back() + " {"));
        for (const std::string &line : bodyLines) {
            output.push_back(this->getIndentedLine(indent + 1, line));
        }
        output.push_back(this->getIndentedLine(indent, "};"));
        return output;
    }

};

#endif
