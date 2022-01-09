#ifndef __DASTGEN2_BitLevelInstructionSet__
#define __DASTGEN2_BitLevelInstructionSet__

#include "DataRepresentation.h"

class ReadBitsExpr : public IExpression {
private:
    IVariableAccess *_bitBankAccess;
    int _bitsCount;
    int _offset;

public:
    ReadBitsExpr(IVariableAccess *memoryBankAccess, int bitsCount, int offset) :
        _bitBankAccess(memoryBankAccess),
        _bitsCount(bitsCount),
        _offset(offset) {}

    IVariableAccess *getBitbankAccess() {
        return this->_bitBankAccess;
    }

    int getBitsCount() {
        return this->_bitsCount;
    }

    int getOffset() {
        return this->_offset;
    }
};

class WriteBitsStmt : public IStatement {
private:
    IVariableAccess *_targetBitBankAccess;
    IExpression *_sourceBitBankAccess;
    int _bitCount;
    int _targetOffset;

public:
    WriteBitsStmt(IExpression *sourceBits, IVariableAccess *targetBits, int bitCount, int targetOffset) :
        _sourceBitBankAccess(sourceBits),
        _targetBitBankAccess(targetBits),
        _bitCount(bitCount),
        _targetOffset(targetOffset) {}

    IExpression *getSource() {
        return this->_sourceBitBankAccess;
    }

    IVariableAccess *getTarget() {
        return this->_targetBitBankAccess;
    }

    int getBitsCount() {
        return this->_bitCount;
    }

    int getTargetOffset() {
        return this->_targetOffset;
    }
};

#endif