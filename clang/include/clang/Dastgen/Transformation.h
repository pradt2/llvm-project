#ifndef __DASTGEN2_CommonTransformation__
#define __DASTGEN2_CommonTransformation__

#include "DataRepresentation.h"

class ITransformation {
public:
    virtual bool supports(DataClass *dataClass) = 0;
    virtual std::vector<DataClass *> transform(DataClass *dataClass) = 0;
};

#endif