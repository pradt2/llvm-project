#ifndef __DASTGEN2_BitsizeDataTypeResolver__
#define __DASTGEN2_BitsizeDataTypeResolver__

#include "DataRepresentation.h"
#include "llvm/Support/Casting.h"

class BitsizeDataTypeResolver {
public:
    PrimitiveDataType resolveDataType(int bitsize) {
        switch (bitsize) {
            case 8: return UNSIGNED_CHAR;
            case 16: return UNSIGNED_SHORT;
            case 32: return UNSIGNED_INT;
            case 64: return UNSIGNED_LONG;
            case 128: return UNSIGNED_LONG_LONG;
        }

        std::cerr << "Unresolvable bitsize" << std::endl;
        return PrimitiveDataType::UNKNOWN;
    }

    int getSize(IDataType *dataType) {
        if (llvm::isa<PrimitiveData>(dataType)) {
            return getSize(reinterpret_cast<PrimitiveData *>(dataType)->getDataType());
        } else if (llvm::isa<EnumDataType>(dataType)) {
            return 32;
        } else {
            std::cerr << "Unknown data type size" << std::endl;
            return 64;
        }
    }

    int getSize(PrimitiveDataType dataType) {
        switch (dataType) {
            case CHAR:
            case UNSIGNED_CHAR:
            case BOOLEAN:
                return 8;
            case SHORT:
            case UNSIGNED_SHORT:
                return 16;
            case INT:
            case UNSIGNED_INT:
            case FLOAT:
                return 32;
            case LONG:
            case UNSIGNED_LONG:
            case DOUBLE:
                return 64;
            case LONG_LONG:
            case UNSIGNED_LONG_LONG:
            case LONG_DOUBLE:
                return 128;
            default:
                std::cerr << "Unknown primitive size" << std::endl;
                return 0;
        }
    }
};

#endif
