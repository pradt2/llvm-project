#ifndef __DASTGEN2_SourceGenerator__
#define __DASTGEN2_SourceGenerator__

#include "DataRepresentation.h"
#include "SourceUnit.h"

class ISourceGenerator {
public:
    virtual std::vector<SourceUnit *> generateSourceUnits(DataClass* dataClass) = 0;
    virtual std::vector<SourceUnit *> generateSourceUnits(const std::vector<DataClass*>& dataClasses) = 0;

};

#endif
