//
// Created by p on 16/02/2022.
//

#ifndef CLANG_INTLIKEBITARRAYCOMPRESSOR_H
#define CLANG_INTLIKEBITARRAYCOMPRESSOR_H

class IntLikeBitArrayCompressor : public AbstractBitArrayCompressor, public NonIndexedFieldCompressor {
  long _rangeMin, _rangeMax;

  long getCompressionConstant() {
    return _rangeMin;
  }

public:

  explicit IntLikeBitArrayCompressor() {}

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, std::string typeStr, long rangeMin, long rangeMax)
      : AbstractBitArrayCompressor(tableCellSize, tableName, typeStr), _rangeMin(rangeMin), _rangeMax(rangeMax) {}

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, FieldDecl *fd)
      : IntLikeBitArrayCompressor(tableCellSize, tableName, fd->getType(), fd->attrs()) {}

  IntLikeBitArrayCompressor(unsigned int tableCellSize, std::string tableName, QualType type, Attrs attrs)
      : AbstractBitArrayCompressor(tableCellSize, tableName, type.getAsString()) {
    for (auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      auto *compressRangeAttr = llvm::cast<CompressRangeAttr>(attr);
      _rangeMin = compressRangeAttr->getMinValue();
      _rangeMax = compressRangeAttr->getMaxValue();
    }
  }

  void setOffset(unsigned int offset) override {
    this->_offset = offset;
  }

  std::string getTypeName() override { return _typeStr; }

  unsigned int getCompressedTypeWidth() override {
    unsigned int valueRange = std::abs(_rangeMax - _rangeMin + 1);
    unsigned int size = ceil(log2(valueRange));
    return size;
  }

  std::string getGetterExpr(std::string thisAccessor) override {
    std::string tableView = getTableViewExpr(thisAccessor);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    std::string getter = "(" + tableView + " & " + afterFetchMask + ")";
    getter = "(" + getter + " >> " + bitshiftAgainstLeftMargin + ")";
    getter = "(" + getter + " + " + compressionConstant + ")";
    getter = "((" + _typeStr + ") " + getter + ")";
    return getter;
  }

  std::string getSetterExpr(std::string thisAccessor, std::string toBeSetValue) override {
    std::string tableView = getTableViewExpr(thisAccessor, false);
    std::string afterFetchMask = std::to_string(getAfterFetchMask()) + "U";
    std::string beforeStoreMask = std::to_string(getBeforeStoreMask()) + "U";
    std::string bitshiftAgainstLeftMargin = std::to_string(getBitsMarginToLeftTableViewEdge()) + "U";
    std::string compressionConstant = std::to_string(getCompressionConstant());
    toBeSetValue = "(" + toBeSetValue + ")";
    toBeSetValue = "(" + toBeSetValue + " - " + compressionConstant + ")";
    toBeSetValue = "(" + toBeSetValue + " << " + bitshiftAgainstLeftMargin + ")";
    toBeSetValue = "(" + toBeSetValue + " & " + afterFetchMask + ")"; // makes sure overflown values do not impact other fields
    std::string valueExpr = "(" + tableView + " & " + beforeStoreMask + ")";
    valueExpr = "(" + valueExpr + " | " + toBeSetValue + ")";
    std::string setterStmt = tableView + " = " + valueExpr;
    return setterStmt;
  }

  std::string getCopyConstructorStmt(std::string thisAccessor, std::string toBeSetVal) override {
    return getSetterExpr(thisAccessor, toBeSetVal) + ";";
  }

  std::string getTypeCastToOriginalStmt(std::string thisAccessor, std::string retValFieldAccessor) override {
    return retValFieldAccessor + " = " + getGetterExpr(thisAccessor) + ";";
  }

  bool supports(FieldDecl *d) override {
    return supports(d->getType(), d->attrs());
  }

  bool supports(QualType type, Attrs attrs) override {
    bool isIntegerType = type->isIntegerType();
    bool hasCompressAttr = false;
    for( auto *attr : attrs) {
      if (!llvm::isa<CompressRangeAttr>(attr)) continue;
      hasCompressAttr = true;
      break;
    }
    return isIntegerType && hasCompressAttr;
  }

};

#endif // CLANG_INTLIKEBITARRAYCOMPRESSOR_H
