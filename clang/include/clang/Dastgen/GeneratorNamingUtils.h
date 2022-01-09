#ifndef __DASTGEN2_GeneratorNamingUtils__
#define __DASTGEN2_GeneratorNamingUtils__

//#include <filesystem>
#include "DataRepresentation.h"

class GeneratorNamingUtils {
public:
    std::string getIfndefBlockName(DataClass *dataClass) {
        std::string blockName;
        for (const auto& ns : dataClass->namespaces) {
            blockName += ns + "__";
        }
        blockName += dataClass->name;
        return blockName;
    }

    std::string getHeaderSourceUnitName(AggregateDataStruct *dataClass) {
      return dataClass->name + ".h";
//        if (dataClass->originalSourceUnitPath.length() == 0) return dataClass->name + ".h";
//        std::filesystem::path filePath(dataClass->originalSourceUnitPath);
//        return filePath.filename().string();
    }

    std::string getImplSourceUnitName(DataClass *dataClass) {
      return dataClass->name + ".cpp";
//        if (dataClass->originalSourceUnitPath.length() == 0) return dataClass->name + ".cpp";
//        std::filesystem::path filePath(dataClass->originalSourceUnitPath);
//        return filePath.filename().string();
    }

    std::vector<std::string> getSourceUnitPath(AggregateDataStruct *dataClass) {
      if (dataClass->namespaces.size() == 0) return std::vector<std::string> {"."};
      return dataClass->namespaces;
//        if (dataClass->originalSourceUnitPath.size() == 0) {
//            if (dataClass->namespaces.size() == 0) return std::vector<std::string> {"."};
//            return dataClass->namespaces;
//        }
//        std::filesystem::path filePath(dataClass->originalSourceUnitPath);
//        return std::vector<std::string> {filePath.parent_path().string()};
    }

    std::vector<std::string> getSourceUnitImportPath(AggregateDataStruct *dataClass) {
      return std::vector<std::string> {"."};
//        if (dataClass->originalSourceUnitPath.size() == 0) {
//            return std::vector<std::string> {"."};
//        }
//        std::filesystem::path filePath(dataClass->originalSourceUnitPath);
//        return std::vector<std::string> {filePath.parent_path().string()};
    }
};

#endif
