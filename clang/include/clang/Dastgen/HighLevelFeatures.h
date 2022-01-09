#include <utility>

#include "DataType.h"
#include "LogicRepresentation.h"

#ifndef __DASTGEN2_HighLevelFeatures__
#define __DASTGEN2_HighLevelFeatures__

class ListDataType : public IDataType {
private:
    IDataType* _itemType;
public:
    ListDataType(PrimitiveDataType itemType) :
        _itemType(new PrimitiveData(itemType)) {}

    ListDataType(IDataType *itemType) :
        _itemType(itemType) {}

    IDataType *getItemType() {
        return this->_itemType;
    }
};

class ListItemAccess : public IVariableAccess {
private:
    IVariableAccess *_listVariable;
    IExpression *_index;
public:
    ListItemAccess(IVariableAccess *listVariable, IExpression *index) :
        _listVariable(listVariable),
        _index(index) {}

    IVariableAccess *getListVariableAccess() {
        return this->_listVariable;
    }

    IExpression *getIndex() {
        return this->_index;
    }
};

class ListItemAssignment: public IAssignStatement {
private:
    IExpression *_index;
public:
    ListItemAssignment(IVariableAccess *var, IExpression *val, IExpression *index) :
        IAssignStatement(var, val),
        _index(index) {}

    IExpression *getIndex() {
        return this->_index;
    }
};

class ListLoadFromArray : public IStatement {
private:
    IVariableAccess *_listAccess;
    IVariableAccess *_arrayAccess;
    IExpression *_arraySize;
public:
    ListLoadFromArray(IVariableAccess *listAccess, IVariableAccess *arrayAccess, IExpression *arraySize) :
        _listAccess(listAccess),
        _arrayAccess(arrayAccess),
        _arraySize(arraySize) {}

    IVariableAccess *getListAccess() {
        return this->_listAccess;
    }

    IVariableAccess *getArrayAccess() {
        return this->_arrayAccess;
    }

    IExpression *getArrSize() {
        return this->_arraySize;
    }
};

class ListLoadToArray : public IStatement {
private:
    IVariableAccess *_listAccess;
    IVariableAccess *_arrayAccess;
public:
    ListLoadToArray(IVariableAccess *listAccess, IVariableAccess *arrayAccess) :
    _listAccess(listAccess),
    _arrayAccess(arrayAccess) {}

    IVariableAccess *getListAccess() {
        return this->_listAccess;
    }

    IVariableAccess *getArrayAccess() {
        return this->_arrayAccess;
    }
};

class ListGetSize : public IExpression {
private:
    IVariableAccess *_list;
public:
    ListGetSize(IVariableAccess *list) :
        _list(list) {}

    IVariableAccess *getListAccess() {
        return this->_list;
    }
};

class ThrowExceptionStmt : public IStatement {
private:
    std::string _message;
public:
    ThrowExceptionStmt(std::string message) :
        _message(std::move(message)) {}

    std::string getMessage() {
        return this->_message;
    }
};

#endif