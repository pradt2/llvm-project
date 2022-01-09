#ifndef __DASTGEN2_ILayoutOptimiser__
#define __DASTGEN2_ILayoutOptimiser__

#include <cmath>
#include "DataRepresentation.h"

class CompressibleField {
private:
    ClassField *_classField;
    int _compressedSize;
public:

    CompressibleField(ClassField *classField, int compressedSize) :
        _classField(classField),
        _compressedSize(compressedSize) {}

    ClassField *getClassField() {
        return this->_classField;
    }

    int getSize() {
        return this->_compressedSize;
    }
};

class MemoryMap {
public:
    std::vector<CompressibleField *> fields;
    int blockCount;
    int blockSize;
};

class ILayoutOptimiser {
protected:

    CompressibleField *getEmptyField(int size) {
        return new CompressibleField(nullptr, size);
    }

    int getTotalBits(const std::vector<CompressibleField *>& fields) {
        if (fields.empty()) return 0;
        int total = 0;
        for (auto field : fields) {
            total += field->getSize();
        }
        return total;
    }

public:
    virtual MemoryMap *getCompressedFields(std::vector<CompressibleField *> fields, int blockSize) = 0;
};

class NopLayoutOptimiser : public ILayoutOptimiser {
public:
    MemoryMap *getCompressedFields(std::vector<CompressibleField *> fields, int blockSize) override {
        MemoryMap *map = new MemoryMap();
        map->blockSize = blockSize;
        map->blockCount = ceil((double) this->getTotalBits(fields) / blockSize);
        map->fields = fields;
        return map;
    };
};

#endif