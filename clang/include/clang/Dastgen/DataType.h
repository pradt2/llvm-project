#ifndef __DASTGEN2_DataType__
#define __DASTGEN2_DataType__

#include <iostream>

enum PrimitiveDataType {
    UNKNOWN,
    VOID,
    BOOLEAN,

    CHAR,
    UNSIGNED_CHAR,

    DOUBLE,
    LONG_DOUBLE,

    FLOAT,

    INT,
    UNSIGNED_INT,

    LONG,
    UNSIGNED_LONG,

    LONG_LONG,
    UNSIGNED_LONG_LONG,

    SHORT,
    UNSIGNED_SHORT,

    STRING
};

class IDataType {
public:
    virtual IDataType *copy() {
        return this;
    }
};

class PrimitiveData: public IDataType {
private:
    PrimitiveDataType _primitiveDataType;
public:
    explicit PrimitiveData(PrimitiveDataType primitiveDataType) :
        _primitiveDataType(primitiveDataType) {}

    PrimitiveDataType getDataType() {
        return this->_primitiveDataType;
    }

    bool equals(PrimitiveDataType primitiveDataType) {
        return this->_primitiveDataType == primitiveDataType;
    }

    PrimitiveData *copy() override {
        return new PrimitiveData(_primitiveDataType);
    }
};

class ConstSizeArrayDataType : public IDataType {
private:
    IDataType* _itemType;
    std::string _itemCount;
public:
    ConstSizeArrayDataType(PrimitiveDataType itemType, unsigned int itemCount) :
            _itemType(new PrimitiveData(itemType)),
            _itemCount(std::to_string(itemCount)) {}

    ConstSizeArrayDataType(IDataType *itemType, std::string itemCount) :
        _itemType(itemType),
        _itemCount(itemCount) {}

    IDataType* getItemType() {
        return this->_itemType;
    }

    std::string getItemCount() {
        return this->_itemCount;
    }

    ConstSizeArrayDataType *copy() override {
        return new ConstSizeArrayDataType(_itemType->copy(), _itemCount);
    }
};

class ReferenceDataType : public IDataType {
private:
    IDataType *_target;
public:
    explicit ReferenceDataType(IDataType *target) :
        _target(target) {}

    IDataType *getTarget() {
        return this->_target;
    }
};

class PointerDataType : public IDataType {
private:
    IDataType *_target;
public:
    explicit PointerDataType(IDataType *target) :
            _target(target) {}

    IDataType *getTarget() {
        return this->_target;
    }
};


enum HighLevelFeatures {
    LIST,
    EXCEPTION
};
#endif
