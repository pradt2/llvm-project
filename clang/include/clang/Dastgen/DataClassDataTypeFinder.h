#ifndef __DASTGEN2_DataClassDataTypeFinder__
#define __DASTGEN2_DataClassDataTypeFinder__

#include "DataRepresentation.h"
#include "LogicRepresentation.h"
#include "HighLevelFeatures.h"
#include "llvm/Support/Casting.h"

class DataClassDataTypeFinder {
private:
    AggregateDataType *checkType(IDataType *dataType) {
        if (llvm::isa<DataClassDataType>(dataType)) {
            return reinterpret_cast<DataClassDataType *>(dataType);
        } else if (llvm::isa<EnumDataType>(dataType)) {
            return reinterpret_cast<EnumDataType *>(dataType);
        } else if (llvm::isa<ReferenceDataType>(dataType)) {
            return this->checkType(reinterpret_cast<ReferenceDataType*>(dataType)->getTarget());
        } else if (llvm::isa<ConstSizeArrayDataType>(dataType)) {
            return this->checkType(reinterpret_cast<ConstSizeArrayDataType*>(dataType)->getItemType());
        } else if (llvm::isa<ListDataType>(dataType)) {
            return this->checkType(reinterpret_cast<ListDataType*>(dataType)->getItemType());
        }
        return nullptr;
    }

    bool isNotSelfDataType(AggregateDataType *dataType, DataClass *dataClass) {
        bool isNotSelf = dataType->getDataStruct() != dataClass;
        return isNotSelf;
    }

public:
    std::vector<AggregateDataType *> findReferencedDataTypes(DataClass *dataClass) {
        std::vector<AggregateDataType *> referencedDataTypes;
        // the helper set enables duplicates detection while maintaining the same order in referencedDataTypes
        std::set<AggregateDataType *> helperSet;
        for (auto field : dataClass->fields) {
            AggregateDataType *dataClassDataType = this->checkType(field->type);
            if (dataClassDataType != nullptr && isNotSelfDataType(dataClassDataType, dataClass) && helperSet.count(dataClassDataType) == 0) {
                referencedDataTypes.push_back(dataClassDataType);
                helperSet.insert(dataClassDataType);
            }
        }
        for (auto constructor : dataClass->constructors) {
            for (auto arg : constructor->methodArguments) {
                AggregateDataType *dataClassDataType = this->checkType(arg->type);
                if (dataClassDataType != nullptr && isNotSelfDataType(dataClassDataType, dataClass) && helperSet.count(dataClassDataType) == 0) {
                    referencedDataTypes.push_back(dataClassDataType);
                    helperSet.insert(dataClassDataType);
                }
            }
        }
        for (auto method : dataClass->methods) {
            for (auto arg : method->methodArguments) {
                AggregateDataType *dataClassDataType = this->checkType(arg->type);
                if (dataClassDataType != nullptr && isNotSelfDataType(dataClassDataType, dataClass) && helperSet.count(dataClassDataType) == 0) {
                    referencedDataTypes.push_back(dataClassDataType);
                    helperSet.insert(dataClassDataType);
                }
            }
        }
        return referencedDataTypes;
    }

    std::vector<AggregateDataType *> findReferencedDataTypes(const std::vector<MethodArgument*>& methodArguments) {
        std::vector<AggregateDataType *> referencedDataTypes;
        for (auto arg : methodArguments) {
            AggregateDataType *dataClassDataType = this->checkType(arg->type);
            if (dataClassDataType != nullptr) referencedDataTypes.push_back(dataClassDataType);
        }
        return referencedDataTypes;
    }
};

#endif
