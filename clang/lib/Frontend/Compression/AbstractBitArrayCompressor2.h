//
// Created by p on 02/09/22.
//

#ifndef CLANG_ABSTRACTBITARRAYCOMPRESSOR2_H
#define CLANG_ABSTRACTBITARRAYCOMPRESSOR2_H

#include <tuple>

struct TableSpec {
  std::string tableAccessor;
  unsigned int cellSize;
  unsigned int cellCount;
};

struct TableArea {
  unsigned int offset;
  unsigned int size;
};

struct ReadingChunk {
  unsigned int cellIdx;
  unsigned int howManyDesiredBitsThisChunkContains;
  unsigned int absoluteOffsetOfTheDesiredBitsThisChunkContains;
  unsigned int offsetWithinDesiredBitsOfTheDesiredBitsThisChunkContains;
  unsigned int chunkSize;
  std::string numericalType;
  unsigned int lsbMargin;
  unsigned int msbMargin;

  ReadingChunk() = default;
  ReadingChunk(TableSpec &spec,
               unsigned int cellIdx,
               unsigned int readsBitsSize,
               unsigned int readsBitsOffset,
               unsigned int desiredBitsOffset,
               unsigned int desiredBitsSize,
               unsigned int chunkSize,
               std::string typeString) : cellIdx(cellIdx),
                                         howManyDesiredBitsThisChunkContains(readsBitsSize),
                                         absoluteOffsetOfTheDesiredBitsThisChunkContains(readsBitsOffset),
                                         offsetWithinDesiredBitsOfTheDesiredBitsThisChunkContains(desiredBitsOffset),
                                         chunkSize(chunkSize) {

    unsigned int readingStartOffset = cellIdx * spec.cellSize;
    this->lsbMargin = readsBitsOffset - readingStartOffset;
    unsigned int readingEndOffset = readingStartOffset + chunkSize;
    this->msbMargin = readingEndOffset > desiredBitsOffset + desiredBitsSize ? readingEndOffset - desiredBitsOffset - desiredBitsSize : 0;
    this->numericalType = std::move(typeString);
  }

  // mask 000011111100000
  unsigned long hillMask() {
    return ((1UL << this->howManyDesiredBitsThisChunkContains) - 1) << this->lsbMargin;
  }

  unsigned long valleyMask() {
    unsigned long chunkSizeMask = this->chunkSize == 64 ? UINT64_MAX : ((1 << this->chunkSize) - 1); // the bitshift trick does not work for the biggest chunk as (1 << 64 -1) equals 0
    return ~hillMask() & chunkSizeMask;
  }
};

unsigned int MAX_CHUNK_SIZE = 64;

class AbstractBitArrayCompressor2 {

  TableSpec spec;

  std::string getReadingChunkType(unsigned int chunkSize) {
    switch (chunkSize) {
    case 8:
      return "unsigned char";
    case 16:
      return "unsigned short";
    case 32:
      return "unsigned int";
    case 64:
      return "unsigned long";
    default:
      llvm::errs() << "Invalid chunk size";
      exit(1);
    }
  }

  std::string getContainerType() {
    if (this->area.size <= 8) return "unsigned char";
    if (this->area.size <= 16) return "unsigned short";
    if (this->area.size <= 32) return "unsigned int";
    if (this->area.size <= 64) return "unsigned long";
    llvm::errs() << "Cannot compute container type";
    exit(1);
  }

  std::tuple<bool, unsigned int>
  canReadUsingChunkSize(unsigned int sizeToRead, unsigned int offset, unsigned int chunkSize) {
    if (sizeToRead > chunkSize) return {false, 0}; // chunk obviously too small
    unsigned int tableSize = this->spec.cellSize * this->spec.cellCount;
    if (chunkSize > tableSize) return {false, 0}; // chunk obviously too big
    unsigned int naiveReadingStartOffset = (offset / this->spec.cellSize) * this->spec.cellSize;
    unsigned int readingEndOffset = offset + sizeToRead;
    if (naiveReadingStartOffset + chunkSize < readingEndOffset)
      return {false,
              0}; // assuming that reading starts as close to the desired bits, if the chunk doesn't cover all the desired bits, then it's too small
    if (naiveReadingStartOffset + chunkSize <= tableSize)
      return {true,
              naiveReadingStartOffset}; // if the chunk isn't too small, if the chunk goes past the end of the table, then it's too big, otherwise it's ok
    naiveReadingStartOffset -= ((naiveReadingStartOffset + chunkSize) / this->spec.cellSize -
                                naiveReadingStartOffset / this->spec.cellSize) *
                               this->spec.cellSize; // moving reading start to that the chunk doesn't go past the table
    if (naiveReadingStartOffset + chunkSize >= readingEndOffset)
      return {true,
              naiveReadingStartOffset}; // if the chunk still covers all desired bits, then it's okay, otherwise it's too small
    return {false, 0};
  }

  unsigned int getChunkSizesCount() {
    unsigned int count = 0;
    unsigned int currentSize = this->spec.cellSize;
    while (currentSize <= MAX_CHUNK_SIZE) {
      count++;
      currentSize = currentSize * 2;
    }
    return count;
  }

  unsigned int getChunkSizeCandidate(unsigned int candidateIdx) {
    return this->spec.cellSize * (1 << candidateIdx);
  }

  ReadingChunk getReadingChunk(unsigned int totalSizeToRead, unsigned int offset) {
    for (unsigned int sizeToRead = totalSizeToRead; sizeToRead >= 1; sizeToRead--) {
      for (unsigned int chunkSizeCandidateIdx = 0;
           chunkSizeCandidateIdx < getChunkSizesCount(); chunkSizeCandidateIdx++) {
        unsigned int chunkSizeCandidate = getChunkSizeCandidate(chunkSizeCandidateIdx);
        std::tuple<bool, unsigned int> tuple = canReadUsingChunkSize(sizeToRead, offset,
                                                                     chunkSizeCandidate);
        if (!std::get<bool>(tuple)) continue;
        unsigned int readingCellIdx = std::get<unsigned int>(tuple) / this->spec.cellSize;
        return ReadingChunk(this->spec,
                            readingCellIdx,
                            sizeToRead,
                            offset,
                            this->area.size - totalSizeToRead,
                            totalSizeToRead,
                            chunkSizeCandidate,
                            this->getReadingChunkType(chunkSizeCandidate));
      }
    }
    llvm::errs() << "Unable to compute the reading chunk size";
    exit(1);
  }

  ReadingChunk getReadingChunk() {
    return getReadingChunk(this->area.size, this->area.offset);
  }

  std::string readChunk(ReadingChunk &chunk) {
    std::string expr = "reinterpret_cast<" + chunk.numericalType + "&>(" + this->spec.tableAccessor + "[" + std::to_string(chunk.cellIdx) + "])"; // access table
    expr = "(" + expr + " & " + std::to_string(chunk.hillMask()) + ")";  // clear out bits that don't belong to the compressed value
    expr = "( (" + this->getContainerType() + ") " + expr + " )"; // widen out the read area to the target size
    int totalOffset = chunk.offsetWithinDesiredBitsOfTheDesiredBitsThisChunkContains - chunk.lsbMargin;
    std::string shiftDir = totalOffset >= 0 ? " << " : " >> ";
    expr = "(" + expr + shiftDir + std::to_string(abs(totalOffset)) + ")";  // position the read bits as they should appear in the container
    return expr;
  }

  std::string writeChunk(ReadingChunk &chunk, std::string valAccessor) {
    std::string tableAccess = "reinterpret_cast<" + chunk.numericalType + "&>(" + this->spec.tableAccessor + "[" + std::to_string(chunk.cellIdx) + "])"; // access table
    std::string tableValueWithClearedOutTargetBits = "(" + tableAccess + " & " + std::to_string(chunk.valleyMask()) + ")";  // clear out bits that belong to the compressed value
    std::string valueBits = "(" + valAccessor + " >> " + std::to_string(chunk.offsetWithinDesiredBitsOfTheDesiredBitsThisChunkContains) + ")"; // bring the desired bits to the start
    valueBits = "(" + valueBits + " & " + std::to_string((chunk.howManyDesiredBitsThisChunkContains == 64 ? UINT64_MAX : 1UL << chunk.howManyDesiredBitsThisChunkContains) - 1) + ")"; // clear out undesired bits from the value
    valueBits = "( (" + chunk.numericalType + ") " + valueBits + ")"; // cast to the numerical type of the table access
    valueBits = "(" + valueBits + " << " + std::to_string(chunk.lsbMargin) + ")"; // align the value bits with the margin of the table
    std::string stmt = tableAccess + " = " + tableValueWithClearedOutTargetBits + " | " + valueBits + ";";
    return stmt;
  }

protected:
  TableArea area;

  AbstractBitArrayCompressor2() {}

  AbstractBitArrayCompressor2(TableSpec spec, TableArea area)
      : spec(spec), area(area) {}

  std::string fetch() {
    std::string expr;
    unsigned int bytesLeftToRead = this->area.size;
    unsigned int offset = this->area.offset;
    while (bytesLeftToRead > 0) {
      ReadingChunk chunk = this->getReadingChunk(bytesLeftToRead, offset);
      expr += readChunk(chunk) + " | ";
      bytesLeftToRead -= chunk.howManyDesiredBitsThisChunkContains;
      offset += chunk.howManyDesiredBitsThisChunkContains;
    }
    if (expr.length() != 0) {
      expr.pop_back();
      expr.pop_back();
      expr.pop_back(); // removing the trailing ' | '
    }
    expr = "(" + expr + ")";
    return expr;
  }

  std::string store(const std::string& valAccessor) {
    std::string stmt;
    unsigned int bytesLeftToWrite = this->area.size;
    unsigned int offset = this->area.offset;
    while (bytesLeftToWrite > 0) {
      ReadingChunk chunk = this->getReadingChunk(bytesLeftToWrite, offset);
      stmt += writeChunk(chunk, valAccessor) + " ";
      bytesLeftToWrite -= chunk.howManyDesiredBitsThisChunkContains;
      offset += chunk.howManyDesiredBitsThisChunkContains;
    }
    return stmt;
  }
};

#endif // CLANG_ABSTRACTBITARRAYCOMPRESSOR2_H
