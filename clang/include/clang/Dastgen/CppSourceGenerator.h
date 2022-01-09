#include "SourceGenerator.h"
#include "CppHeaderSourceUnitGenerator.h"
#include "CppImplSourceUnitGenerator.h"

class CppSourceGenerator : public ISourceGenerator,
                           private CppHeaderSourceUnitGenerator,
                           private CppImplSourceUnitGenerator {
public:
    std::vector<SourceUnit *> generateSourceUnits(DataClass *dataClass) override {
        return std::vector<SourceUnit *> {
            this->generateHeaderSourceUnit(dataClass),
            this->generateImplSourceUnit(dataClass)
        };
    }

    std::vector<SourceUnit *> generateSourceUnits(const std::vector<DataClass*>& dataClasses) override {
        std::vector<SourceUnit *> sources;
        for (auto dataClass : dataClasses) {
            std::vector<SourceUnit *> classSources = this->generateSourceUnits(dataClass);
            sources.insert(sources.end(), classSources.begin(), classSources.end());
        }
        return sources;
    }
};
