#ifndef __DASTGEN2_BitLevelInstructionSetResolver__
#define __DASTGEN2_BitLevelInstructionSetResolver__

#include <cmath>
#include "BitLevelInstructionSet.h"

class BitLevelInstructionSetResolver {
private:
    unsigned long cutToBits(unsigned long val, int bitsCount) {
        unsigned long result = val & ~(-1UL << bitsCount);
        return result;
    }

    unsigned long getMaskRight(int blockSize, int bitsCount) {
        unsigned long result = cutToBits(-1UL << bitsCount, blockSize);
        return result;
    }

    unsigned long getMaskLeft(int blockSize, int bitsCount) {
        unsigned long result = cutToBits(~(-1UL << (blockSize - bitsCount)), blockSize);
        return result;
    }

    unsigned long getMask(int blockSize, int maskBitsLeft, int maskBitsRight) {
        return getMaskLeft(blockSize, maskBitsLeft) & getMaskRight(blockSize, maskBitsRight);
    };

    IExpression *readBitsFromBlock(IVariableAccess *memBankAccess, int blockSize, int blockIndex,
                                   int intraBlockOffset, int bitsCount) {
        int rightMaskBitsCount = intraBlockOffset;
        int leftMaskBitsCount = blockSize - bitsCount - intraBlockOffset;
        unsigned long mask = getMask(blockSize, leftMaskBitsCount, rightMaskBitsCount);
        IExpression *bankAccess = new ArrayVariableAccess(memBankAccess, new ConstExpr(std::to_string(blockIndex)));
        IExpression *maskExpr = new IsolatedEvaluation(new BitwiseAnd(bankAccess, new ConstExpr(std::to_string(mask))));
        IExpression *shiftExpr = new IsolatedEvaluation(new BitshiftRight(maskExpr, new ConstExpr(std::to_string(intraBlockOffset))));
        return shiftExpr;
    }

    IStatement *writeBitsToBlock(IExpression *sourceAccess, IVariableAccess *destAccess,
                                 int blockSize, int blockIndex, int intraBlockOffset, int bitsCount) {
        IExpression *sourceAccessAligned = new BitshiftLeft(new IsolatedEvaluation(sourceAccess), new LiteralValue(intraBlockOffset));
        IVariableAccess *destByteAccess = new ArrayVariableAccess(destAccess, new ConstExpr(std::to_string(blockIndex)));
        int rightMaskBitsCount = intraBlockOffset;
        int leftMaskBitsCount = blockSize - bitsCount - intraBlockOffset;
        unsigned long clearBitsMask = cutToBits(~getMask(blockSize, leftMaskBitsCount, rightMaskBitsCount), blockSize);
        IExpression *clearedBits = new IsolatedEvaluation(new BitwiseAnd(destByteAccess, new ConstExpr(std::to_string(clearBitsMask))));
        IExpression *newValExpr = new IsolatedEvaluation(new BitwiseOr(clearedBits, sourceAccessAligned));
        IStatement *assignExpr = new ValueAssignment(destByteAccess, newValExpr);
        return assignExpr;
    }

public:

    IExpression *resolveReadBits(ReadBitsExpr *readExpr, int blockSize) {
        int startingBlockIndex = readExpr->getOffset() / blockSize;
        int endBlockIndex = ceil((double) (readExpr->getOffset() + readExpr->getBitsCount()) / blockSize);
        int intraBlockOffset = readExpr->getOffset() - startingBlockIndex * blockSize;
        int remainingBits = readExpr->getBitsCount();
        IExpression *bitsSoFar = nullptr;
        for (int index = startingBlockIndex; index < endBlockIndex; index++) {
            int bitsToBeRead = remainingBits + intraBlockOffset <= blockSize
                    ? remainingBits
                    : blockSize - intraBlockOffset;
            IExpression *readBits = readBitsFromBlock(readExpr->getBitbankAccess(), blockSize, index, intraBlockOffset, bitsToBeRead);
            int bitshift = readExpr->getBitsCount() - remainingBits;
            remainingBits -= bitsToBeRead;
            readBits = new IsolatedEvaluation(new BitshiftLeft(readBits, new ConstExpr(std::to_string(bitshift))));
            if (bitsSoFar == nullptr) bitsSoFar = readBits;
            else bitsSoFar = new BitwiseOr(bitsSoFar, readBits);
            intraBlockOffset = 0;
        }
        return bitsSoFar;
    }

    std::vector<IStatement*> resolveWriteBits(WriteBitsStmt *writeStmt, int blockSize) {
        int startingBlockIndex = writeStmt->getTargetOffset() / blockSize;
        int endBlockIndex = ceil((double) (writeStmt->getTargetOffset() + writeStmt->getBitsCount()) / blockSize);
        int intraBlockOffset = writeStmt->getTargetOffset() - startingBlockIndex * blockSize;
        int remainingBits = writeStmt->getBitsCount();
        std::vector<IStatement*> statements;
        for (int index = startingBlockIndex; index < endBlockIndex; index++) {
            int bitsToBeWritten = remainingBits + intraBlockOffset <= blockSize
                               ? remainingBits
                               : blockSize - intraBlockOffset;
            IExpression *sourceToBeWritten = new BitshiftRight(writeStmt->getSource(), new LiteralValue(writeStmt->getBitsCount() - remainingBits));
            IStatement *writeBits = writeBitsToBlock(sourceToBeWritten, writeStmt->getTarget(), blockSize, index, intraBlockOffset, bitsToBeWritten);
            statements.emplace_back(writeBits);
            remainingBits -= bitsToBeWritten;
            intraBlockOffset = 0;
        }
        return statements;
    }

};

#endif
