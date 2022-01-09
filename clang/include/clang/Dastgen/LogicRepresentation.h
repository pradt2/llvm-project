#include <string>
#include <utility>
#include <vector>

#include "DataType.h"

#ifndef __DASTGEN2_LogicRepresentation__
#define __DASTGEN2_LogicRepresentation__

class IExpression {
    virtual bool iHateCpp() {return true;}
};

class ConstExpr : public IExpression {
private:
    std::string _value;
public:
    ConstExpr(std::string value) :
        _value(std::move(value)) {}

    std::string getValue() {
        return this->_value;
    }
};

class IVariableAccess : public IExpression {};

class LocalVariableAccess : public IVariableAccess {
private:
    std::string identifier;
public:
    explicit LocalVariableAccess(std::string variableName) :
        identifier(std::move(variableName)) {};

    std::string getVariableName() {
            return this->identifier;
    }

    void setVariableName(std::string variableName) {
        this->identifier = std::move(variableName);
    }
};

class ClassFieldVariableAccess : public IVariableAccess {
private:
    IVariableAccess *innerVariableAccess;

public:
    explicit ClassFieldVariableAccess(IVariableAccess *asLocalVariable) :
        innerVariableAccess(asLocalVariable) {}

    IVariableAccess *getVariable() {
        return this->innerVariableAccess;
    }
};

class ArrayVariableAccess : public IVariableAccess {
private:
    IExpression* arrayAccess;
    IExpression* index;
public:
    ArrayVariableAccess(std::string variableName, int arrayIndex) :
        arrayAccess(new LocalVariableAccess(std::move(variableName))),
        index(new ConstExpr(std::to_string(arrayIndex))) {}

    ArrayVariableAccess(IExpression* variable, IExpression *arrayIndex) :
        arrayAccess(variable),
        index(arrayIndex) {}

    IExpression* getArrayAccess() {
        return this->arrayAccess;
    }

    IExpression* getIndex() {
        return this->index;
    }
};

class ObjectFieldVariableAccess : public IVariableAccess {
private:
    IVariableAccess *_object;
    std::string _fieldName;
public:
    ObjectFieldVariableAccess(IVariableAccess *object, std::string fieldName) :
        _object(object),
        _fieldName(std::move(fieldName)) {}

    IVariableAccess *getObject() {
        return this->_object;
    }

    std::string getFieldName() {
        return this->_fieldName;
    }
};

class ITwoHandsExpression : public IExpression {
private:
    IExpression *leftExpression, *rightExpression;

public:
    ITwoHandsExpression(IExpression *lhs, IExpression *rhs) :
        leftExpression(lhs),
        rightExpression(rhs) {}

    IExpression *getLeftExpression() {
        return this->leftExpression;
    }

    IExpression *getRightExpression() {
        return this->rightExpression;
    }
};

class BitwiseAnd : public ITwoHandsExpression {
public:
    BitwiseAnd(IExpression *lhs, IExpression *rhs) :
            ITwoHandsExpression(lhs, rhs) {}
};

class BitwiseOr : public ITwoHandsExpression {
public:
    BitwiseOr(IExpression *lhs, IExpression *rhs) :
            ITwoHandsExpression(lhs, rhs) {}
};

class Addition : public ITwoHandsExpression {
public:
    Addition(IExpression *lhs, IExpression *rhs) :
        ITwoHandsExpression(lhs, rhs) {}
};

class Subtraction : public ITwoHandsExpression {
public:
    Subtraction(IExpression *lhs, IExpression *rhs) :
        ITwoHandsExpression(lhs, rhs) {}
};

class EqualityComparison : public ITwoHandsExpression {
public:
    EqualityComparison(IExpression *lhs, IExpression *rhs) :
        ITwoHandsExpression(lhs, rhs) {}
};

class IBitshift : public ITwoHandsExpression {
public:
    IBitshift(IExpression *subject, IExpression *amount) :
            ITwoHandsExpression(subject, amount) {}
public:
    IExpression *getBitshiftSubject() {
        return this->getLeftExpression();
    }

    IExpression *getBitshiftAmount() {
        return this->getRightExpression();
    }

};

class BitshiftRight : public IBitshift {
public:
    BitshiftRight(IExpression *subject, IExpression *amount) :
            IBitshift(subject, amount) {}
};

class BitshiftLeft : public IBitshift {
public:
    BitshiftLeft(IExpression *subject, IExpression *amount) :
            IBitshift(subject, amount) {}
};

class IsolatedEvaluation : public IExpression {
private:
    IExpression *innerExpr;
public:
    explicit IsolatedEvaluation(IExpression *protectedExpr) :
        innerExpr(protectedExpr) {}

    IExpression *getProtectedExpression() {
        return this->innerExpr;
    }
};

class ConditionalExpression : public IExpression {
private:
    IExpression *_condition;
    IExpression *_trueVal;
    IExpression *_falseVal;

public:
    ConditionalExpression(IExpression *condition, IExpression *trueVal, IExpression *falseVal) :
        _condition(condition),
        _trueVal(trueVal),
        _falseVal(falseVal) {}

    IExpression *getCondition() {
        return this->_condition;
    }

    IExpression *getTrueVal() {
        return this->_trueVal;
    }

    IExpression *getFalseVal() {
        return this->_falseVal;
    }
};

class ITypeCast : public IExpression {
private:
    IDataType* type;
    IExpression *expression;

public:
    ITypeCast(PrimitiveDataType targetType, IExpression *expr) :
    type(new PrimitiveData(targetType)),
    expression(expr) {}

    ITypeCast(IDataType* targetType, IExpression *expr) :
    type(targetType),
    expression(expr) {}

    IDataType *getTargetType() {
        return this->type;
    }

    IExpression *getCastExpr() {
        return this->expression;
    }
};

class ExpressionCast : public ITypeCast {
public:
    ExpressionCast(PrimitiveDataType targetType, IExpression *expr)
        : ITypeCast(targetType, expr) {}

    ExpressionCast(IDataType *targetType, IExpression *expr)
        : ITypeCast(targetType, expr) {}
};

class ReinterpretCast : public ITypeCast {
public:
    ReinterpretCast(PrimitiveDataType targetType, IExpression *expr)
            : ITypeCast(targetType, expr) {}

    ReinterpretCast(IDataType *targetType, IExpression *expr)
            : ITypeCast(targetType, expr) {}
};

class LiteralValue : public IExpression {
private:
    std::string valueLiteral;
public:
    explicit LiteralValue(std::string val) :
        valueLiteral(std::move(val)) {}

    explicit LiteralValue(int val) :
        valueLiteral(std::to_string(val)) {}

    explicit LiteralValue(long val) :
        valueLiteral(std::to_string(val)) {}

    explicit LiteralValue(unsigned long val) :
        valueLiteral(std::to_string(val)) {}

    explicit LiteralValue(unsigned int val) :
        valueLiteral(std::to_string(val)) {}

    explicit LiteralValue(unsigned char val) :
            valueLiteral(std::to_string(static_cast<unsigned short>(val))) {}

    std::string getLiteralValue() {
        return this->valueLiteral;
    }
};

class MethodInvocation : public IExpression {
private:
    IVariableAccess *_target;
    std::string _methodName;
    std::vector<IExpression *> _args;
public:
    MethodInvocation(IVariableAccess *target, std::string methodName, std::vector<IExpression *> args) :
            _target(target),
            _methodName(std::move(methodName)),
            _args(std::move(args)) {};

    IVariableAccess *getTarget() {
        return this->_target;
    }

    std::string getMethodName() {
        return this->_methodName;
    }

    std::vector<IExpression *> getArgs() {
        return this->_args;
    }
};

class ClassInstanceMethodInvocation : public IExpression {
private:
    std::string _methodName;
    std::vector<IExpression *> _args;
public:
    ClassInstanceMethodInvocation(std::string methodName, std::vector<IExpression *> args) :
    _methodName(std::move(methodName)),
    _args(std::move(args)) {};

    std::string getMethodName() {
        return this->_methodName;
    }

    std::vector<IExpression *> getArgs() {
        return this->_args;
    }
};

class IStatement {
private:
    virtual bool iHateCpp() {return true;}
};

class IAssignStatement : public IStatement {
private:
    IVariableAccess *variableAccess;
    IExpression *value;

public:
    IAssignStatement(IVariableAccess *var, IExpression *val) :
        variableAccess(var),
        value(val){}

public:
    IVariableAccess *getVariable() {
        return this->variableAccess;
    }

    IExpression *getValue() {
        return this->value;
    }
};

class ValueAssignment : public IAssignStatement {
public:
    ValueAssignment(IVariableAccess *var, IExpression *val) :
            IAssignStatement(var, val) {}
};

class BitwiseOrAssignment : public IAssignStatement {
public:
    BitwiseOrAssignment(IVariableAccess *var, IExpression *val) :
            IAssignStatement(var, val) {}
};

class BitwiseAndAssignment : public IAssignStatement {
public:
    BitwiseAndAssignment(IVariableAccess *var, IExpression *val) :
            IAssignStatement(var, val) {}
};

class ReturnStatement : public IStatement {
private:
    IExpression *returnVal;
public:
    explicit ReturnStatement(IExpression *val) :
        returnVal(val) {}

    IExpression *getReturnValue() {
        return this->returnVal;
    }
};

class VariableDeclaration : public IStatement {
private:
    IDataType *variableType;
    std::string variableName;
public:
    VariableDeclaration(IDataType *type, std::string name) :
        variableType(type),
        variableName(std::move(name)) {}

    IDataType *getType() {
        return this->variableType;
    }

    std::string getName() {
        return this->variableName;
    }
};

class ExpressionStatement : public IStatement {
private:
    IExpression *_expression;
public:
    explicit ExpressionStatement(IExpression *expression) :
        _expression(expression) {}

    IExpression *getExpression() {
        return this->_expression;
    }
};

class IMultilineStatement : public IStatement {
private:
    std::vector<IStatement *> _innerStatements;
public:
    explicit IMultilineStatement(std::vector<IStatement *> innerStatements) :
        _innerStatements(std::move(innerStatements)) {}

    std::vector<IStatement *> getInnerStatement() {
        return this->_innerStatements;
    }
};

class LoopStatement : public IMultilineStatement {
private:
    IExpression *_iterCount;
    LocalVariableAccess *_iterCounter;
public:
    LoopStatement(LocalVariableAccess *iterCounter, IExpression *iterCount, std::vector<IStatement *> innerStatements) :
        IMultilineStatement(std::move(innerStatements)),
        _iterCount(iterCount),
        _iterCounter(iterCounter) {}

    IExpression *getIterCount() {
        return this->_iterCount;
    }

    LocalVariableAccess *getIterCounter() {
        return this->_iterCounter;
    }
};

#endif
