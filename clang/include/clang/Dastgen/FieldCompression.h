#ifndef __DASTGEN2_FieldCompressor__
#define __DASTGEN2_FieldCompressor__

#include <cmath>
#include "BitsizeDataTypeResolver.h"
#include "DataRepresentation.h"
#include "llvm/Support/Casting.h"

class IFieldCompressor {
public:
    virtual bool isSupported(ClassField *classField) { std::cerr << "Override me!" << std::endl; return false; };
    virtual int getCompressedSize(ClassField *classField) { std::cerr << "Override me!" << std::endl; return 0; };

    virtual IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) {
        std::cerr << "Override me!" << std::endl; return nullptr;
    };

    virtual IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) {
        std::cerr << "Override me!" << std::endl; return nullptr;
    };
};

class BoolFieldCompressor : public IFieldCompressor {

public:
  bool isSupported(ClassField *classField) override {
    if (llvm::isa<PrimitiveData>(classField->type)) return false;
    PrimitiveData *data = reinterpret_cast<PrimitiveData*>(classField->type);
    return data->equals(PrimitiveDataType::BOOLEAN);
  }

  int getCompressedSize(ClassField *classField) override {
    return 1;
  }

  IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) override {
    return rawVal;
  }

  IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) override {
    return new ExpressionCast(PrimitiveDataType::BOOLEAN, compressedVal);
  }
};

class EnumFieldCompressor : public IFieldCompressor {
private:

  unsigned int getMinValue(std::vector<EnumValue> values) {
    unsigned int minVal = -1;
    for (auto &value : values) {
      unsigned int val = value.getValue();
      if (val < minVal) minVal = val;
    }
    return minVal;
  }

  unsigned int getMaxVal(std::vector<EnumValue> values) {
    unsigned int maxVal = 0;
    for (auto &value : values) {
      unsigned int val = value.getValue();
      if (val > maxVal) maxVal = val;
    }
    return maxVal;
  }

  bool areValuesContinuous(const std::vector<EnumValue>& values) {
    unsigned int minVal = this->getMinValue(values);
    unsigned int maxVal = this->getMaxVal(values);
    unsigned int range = maxVal - minVal + 1;
    if (range <= values.size()) return true;
    return false;
  }

  IExpression *getCompressedExprForContinuousVals(EnumDataType *enumType, IExpression *rawVal) {
    int minVal = this->getMinValue(enumType->getTargetEnum()->values);
    return new ExpressionCast(enumType, new Subtraction(rawVal, new LiteralValue(minVal)));
  }

  IExpression *getDecompressedExprForContinuousVals(EnumDataType *enumType, IExpression *compressedVal) {
    int minVal = this->getMinValue(enumType->getTargetEnum()->values);
    return new ExpressionCast(enumType, new Addition(compressedVal, new LiteralValue(minVal)));
  }

  IExpression *getCompressedExprForDiscontinuousVals(EnumDataType *enumType, IExpression *rawVal) {
    IExpression *elseVal = new LiteralValue(0);
    int valIndex = 0;
    for (auto &value : enumType->getTargetEnum()->values) {
      IExpression *comparison = new EqualityComparison(rawVal, new LiteralValue(value.getValue()));
      IExpression *trueVal = new LiteralValue(valIndex);
      elseVal = new ConditionalExpression(comparison, trueVal, elseVal);
      valIndex++;
    }
    return new ExpressionCast(enumType, elseVal);
  }

  IExpression *getDecompressedExprForDiscontinuousVals(EnumDataType *enumType, IExpression *compressedVal) {
    IExpression *elseVal = new LiteralValue(0);
    int valIndex = 0;
    for (auto &value : enumType->getTargetEnum()->values) {
      IExpression *comparison = new EqualityComparison(compressedVal, new LiteralValue(valIndex));
      IExpression *trueVal = new LiteralValue(value.getValue());
      elseVal = new ConditionalExpression(comparison, trueVal, elseVal);
      valIndex++;
    }
    elseVal = new ExpressionCast(enumType, elseVal);
    return elseVal;
  }


public:
  bool isSupported(ClassField *classField) override {
    if (!llvm::isa<EnumDataType>(classField->type)) return false;
    EnumDataType *enumDataType = reinterpret_cast<EnumDataType *>(classField->type);
    if (enumDataType->getTargetEnum()->values.size() < 2) return false;
    return true;
  }

  int getCompressedSize(ClassField *classField) override {
    EnumDataType *enumType = reinterpret_cast<EnumDataType *>(classField->type);
    int valueCount = enumType->getTargetEnum()->values.size();
    int size = ceil(log2(valueCount));
    return size;
  }

  IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) override {
    EnumDataType *enumDataType = reinterpret_cast<EnumDataType *>(compressedField->type);
    if (this->areValuesContinuous(enumDataType->getTargetEnum()->values)) {
      return this->getCompressedExprForContinuousVals(enumDataType, rawVal);
    }
    return this->getCompressedExprForDiscontinuousVals(enumDataType, rawVal);
  }

  IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) override {
    EnumDataType *enumDataType = reinterpret_cast<EnumDataType *>(compressedField->type);
    if (this->areValuesContinuous(enumDataType->getTargetEnum()->values)) {
      return this->getDecompressedExprForContinuousVals(enumDataType, compressedVal);
    }
    return this->getDecompressedExprForDiscontinuousVals(enumDataType, compressedVal);
  }

};

class IntegerRangeFieldCompressor : public IFieldCompressor {
private:
  const std::string intRangeAnnotation = "int_range";

  long getRangeLowerBound(ClassField *classField) {
    std::string lowerBoundStr;
    for (auto annotation : classField->annotations) {
      if (annotation->tokens[0] != intRangeAnnotation) continue;
      lowerBoundStr = annotation->tokens[1];
    }
    if (lowerBoundStr.empty()) {
      std::cerr << "Integer range lower bound is not specified" << std::endl;
      return 0;
    }
    long lowerBound = std::stol(lowerBoundStr);
    return lowerBound;
  }

  unsigned long getRangeUpperBound(ClassField *classField) {
    std::string upperBoundStr;
    for (auto annotation : classField->annotations) {
      if (annotation->tokens[0] != intRangeAnnotation) continue;
      if (annotation->tokens.size() != 3) {
        std::cerr << "Integer range upper bound is not specified" << std::endl;
        return 0;
      }
      upperBoundStr = annotation->tokens[2];
    }
    if (upperBoundStr.empty()) {
      std::cerr << "Integer range upper bound is not specified" << std::endl;
      return 0;
    }
    unsigned long upperBound = std::stoul(upperBoundStr);
    return upperBound;
  }

public:

  bool isSupported(ClassField *classField) override {
    bool isSupportedType = false;
    if (!llvm::isa<PrimitiveData*>(classField->type)) return false;
    PrimitiveData *dataType = reinterpret_cast<PrimitiveData*>(classField->type);
    switch (dataType->getDataType()) {
    case CHAR:
    case UNSIGNED_CHAR:
    case SHORT:
    case UNSIGNED_SHORT:
    case INT:
    case UNSIGNED_INT:
    case LONG:
    case UNSIGNED_LONG:
    case LONG_LONG:
    case UNSIGNED_LONG_LONG:
      isSupportedType = true;
      break;
    default:
      break;
    }
    if (!isSupportedType) return false;

    for (auto annotation : classField->annotations) {
      if (annotation->tokens[0] == intRangeAnnotation) return true;
    }

    return false;
  }

  int getCompressedSize(ClassField *classField) override {
    long lowerBound = this->getRangeLowerBound(classField);
    unsigned long upperBound = this->getRangeUpperBound(classField);
    unsigned long range = upperBound - lowerBound + 1;
    int rangeBits = ceil(log2(range));
    return rangeBits;
  }

  IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) override {
    return new Subtraction(rawVal, new LiteralValue(this->getRangeLowerBound(compressedField)));
  };

  IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) override {
    IExpression *val = new Addition(compressedVal, new LiteralValue(this->getRangeLowerBound(compressedField)));
    return new ExpressionCast(reinterpret_cast<PrimitiveData *>(compressedField->type), val);
  };
};

struct FloatFormat {
  std::string sign;
  unsigned int exponentBits;
  unsigned int bias;
  unsigned int significandBits;
};

class FloatFieldCompressor : public IFieldCompressor {
private:

  const std::string floatStandard = "float_standard";
  const std::string floatStandardPrefix = "float";
  const std::string float16 = floatStandardPrefix + "16";
  const std::string float32 = floatStandardPrefix + "32";
  const std::string float64 = floatStandardPrefix + "64";
  const std::string float128 = floatStandardPrefix + "128";


  const std::string floatFormat = "float_format";
  const std::string signPositive = "positive";
  const std::string signNegative = "negative";
  const std::string signMixed = "mixed_sign";

  BitsizeDataTypeResolver bitsizeDataTypeResolver;

  FloatFormat binary16 {
      .sign = signMixed,
      .exponentBits = 5,
      .bias = 15,
      .significandBits = 10
  };

  FloatFormat binary32 {
      .sign = signMixed,
      .exponentBits = 8,
      .bias = 127,
      .significandBits = 23
  };

  FloatFormat binary64 {
      .sign = signMixed,
      .exponentBits = 11,
      .bias = 1023,
      .significandBits = 52
  };

  FloatFormat binary128 {
      .sign = signMixed,
      .exponentBits = 15,
      .bias = 16383,
      .significandBits = 112
  };

  FloatFormat *resolveSourceFormat(ClassField *classField) {
    if (!llvm::isa<PrimitiveData>(classField->type)) {
      std::cerr << "Cannot parse float type of a non-primitive class field" << std::endl;
      return nullptr;
    }
    PrimitiveDataType fieldType = reinterpret_cast<PrimitiveData *>(classField->type)->getDataType();
    switch (fieldType) {
    case FLOAT:
      return &binary32;
    case DOUBLE:
      return &binary64;
    case LONG_DOUBLE:
      return &binary128;
    default:
      std::cerr << "Cannot parse float type of a primitive non-float class field" << std::endl;
      return nullptr;
    }
  }

  bool isFloatStandardAnnotation(Annotation *annotation) {
    bool isFloatStandard = !annotation->tokens.empty() && annotation->tokens[0] == floatStandard;
    return isFloatStandard;
  }

  bool isFloatStandard(ClassField *classField) {
    for (auto annotation : classField->annotations) {
      if (this->isFloatStandardAnnotation(annotation)) return true;
    }
    return false;
  }

  bool isFloatFormat(ClassField *classField) {
    for (auto annotation : classField->annotations) {
      if (this->isFloatFormatAnnotation(annotation)) return true;
    }
    return false;
  }

  FloatFormat *parseFloatStandard(ClassField *classField) {
    Annotation *annotation = nullptr;
    for (auto a : classField->annotations) {
      if (!this->isFloatStandardAnnotation(a)) continue;
      annotation = a;
      break;
    }
    if (annotation == nullptr) {
      std::cerr << "Failed to parse float standard" << std::endl;
      return nullptr;
    }
    std::string standardCandidate = annotation->tokens[1];
    if (standardCandidate == float16) {
      return &binary16;
    }
    if (standardCandidate == float32) {
      return &binary32;
    }
    if (standardCandidate == float64) {
      return &binary64;
    }
    if (standardCandidate == float128) {
      return &binary128;
    }
    std::cerr << "Unknown standard float format" << std::endl;
    return nullptr;
  }

  bool isFloatFormatAnnotation(Annotation *annotation) {
    bool isFloatFormat = !annotation->tokens.empty() && annotation->tokens[0] == floatFormat;
    return isFloatFormat;
  }

  FloatFormat *parseCustomFormat(ClassField *classField) {
    Annotation *annotation = nullptr;
    for (auto a : classField->annotations) {
      if (!this->isFloatFormatAnnotation(a)) continue;
      annotation = a;
      break;
    }
    if (annotation == nullptr) {
      std::cerr << "Failed to parse float format" << std::endl;
      return nullptr;
    }
    std::string signCandidate = annotation->tokens[1];
    if (signCandidate != signPositive && signCandidate != signNegative && signCandidate != signMixed) {
      std::cerr << "Unknown sign value, possible values are " << signPositive << " " << signNegative << " " << signMixed << std::endl;
      return nullptr;
    }

    unsigned int exponentBits = std::stoi(annotation->tokens[2]);
    unsigned int exponentBias = std::stoi(annotation->tokens[3]);
    unsigned int significandBits = std::stoi(annotation->tokens[4]);
    return new FloatFormat{
        .sign = signMixed,
        .exponentBits = exponentBits,
        .bias = exponentBias,
        .significandBits = significandBits
    };
  }

  FloatFormat *getTargetFloatFormat(ClassField *classField) {
    FloatFormat *floatFormat = nullptr;
    if (this->isFloatStandard(classField)) {
      floatFormat = this->parseFloatStandard(classField);
    } else if (this->isFloatFormat(classField)) {
      floatFormat = this->parseCustomFormat(classField);
    }
    return floatFormat;
  }

  int getSize(FloatFormat *floatFormat) {
    int size = 0;
    if (floatFormat->sign == signMixed) size += 1;
    size += floatFormat->exponentBits;
    size += floatFormat->significandBits;
    return size;
  }

  IExpression *getSignificandExpr(FloatFormat *sourceFormat, FloatFormat *targetFormat, IExpression *rawVal) {
    IExpression *shiftExpr = nullptr;
    int newBitsOffset = targetFormat->significandBits - sourceFormat->significandBits;
    if (newBitsOffset >= 0) {
      shiftExpr = new BitshiftLeft(rawVal, new LiteralValue(newBitsOffset));
    } else {
      shiftExpr = new BitshiftRight(rawVal, new LiteralValue(-newBitsOffset));
    }
    unsigned long mask = (1 << targetFormat->significandBits) - 1;
    shiftExpr = new BitwiseAnd(new IsolatedEvaluation(shiftExpr), new ExpressionCast(UNSIGNED_LONG, new LiteralValue(mask)));
    shiftExpr = new IsolatedEvaluation(shiftExpr);
    return shiftExpr;
  }

  IExpression *getExponentExpr(FloatFormat *sourceFormat, FloatFormat *targetFormat, IExpression *rawVal) {
    IExpression *expOnlyExpr = new BitshiftRight(rawVal, new LiteralValue(sourceFormat->significandBits));
    expOnlyExpr = new IsolatedEvaluation(expOnlyExpr);
    unsigned int mask = (1 << sourceFormat->exponentBits) - 1;
    expOnlyExpr = new BitwiseAnd(expOnlyExpr, new LiteralValue(mask));
    expOnlyExpr = new ExpressionCast(UNSIGNED_LONG, expOnlyExpr);
    int exponentBiasDiff = ((int)targetFormat->bias) - sourceFormat->bias;
    expOnlyExpr = new IsolatedEvaluation(new Addition(expOnlyExpr, new LiteralValue(exponentBiasDiff)));
    expOnlyExpr = new BitshiftLeft(expOnlyExpr, new LiteralValue(targetFormat->significandBits));
    expOnlyExpr = new IsolatedEvaluation(expOnlyExpr);
    return expOnlyExpr;
  }

  IExpression *getSignExpr(FloatFormat *sourceFormat, FloatFormat *targetFormat, IExpression *rawVal) {
    if (targetFormat->sign == signPositive || sourceFormat->sign == signPositive) return new LiteralValue(0);
    if (targetFormat->sign == signNegative || sourceFormat->sign == signNegative)
      return new LiteralValue(1UL << (targetFormat->exponentBits + targetFormat->significandBits));
    IExpression *shiftExpr = nullptr;
    //positive to the left
    int newBitsOffset = targetFormat->exponentBits
                        + targetFormat->significandBits
                        - sourceFormat->exponentBits
                        - sourceFormat->significandBits;
    if (newBitsOffset >= 0) {
      shiftExpr = new BitshiftLeft(rawVal, new LiteralValue(newBitsOffset));
    } else {
      shiftExpr = new BitshiftRight(rawVal, new LiteralValue(-newBitsOffset));
    }
    unsigned long mask = 1UL << (targetFormat->exponentBits + targetFormat->significandBits);
    shiftExpr = new BitwiseAnd(new IsolatedEvaluation(shiftExpr), new ExpressionCast(UNSIGNED_LONG, new LiteralValue(mask)));
    shiftExpr = new IsolatedEvaluation(shiftExpr);
    return shiftExpr;
  }

public:

  bool isSupported(ClassField *classField) override {
    bool isSupportedType = false;
    if (!llvm::isa<PrimitiveData*>(classField->type)) return false;
    PrimitiveData *dataType = reinterpret_cast<PrimitiveData*>(classField->type);
    switch (dataType->getDataType()) {
    case FLOAT:
    case DOUBLE:
    case LONG_DOUBLE:
      isSupportedType = true;
      break;
    default:
      break;
    }

    if (!isSupportedType) return false;

    if (this->isFloatFormat(classField)) return true;
    if (this->isFloatStandard(classField)) return true;

    return false;
  }

  int getCompressedSize(ClassField *classField) override {
    FloatFormat *floatFormat = this->getTargetFloatFormat(classField);
    int size = this->getSize(floatFormat);
    return size;
  }

  IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) override {
    PrimitiveData *dataType = reinterpret_cast<PrimitiveData *>(compressedField->type);
    int typeSize = bitsizeDataTypeResolver.getSize(dataType->getDataType());
    PrimitiveDataType containerType = bitsizeDataTypeResolver.resolveDataType(typeSize);
    rawVal = new ReinterpretCast(new ReferenceDataType(new PrimitiveData(containerType)), rawVal);
    FloatFormat *sourceFormat = this->resolveSourceFormat(compressedField);
    FloatFormat *targetFormat = this->getTargetFloatFormat(compressedField);
    IExpression *significand = this->getSignificandExpr(sourceFormat, targetFormat, rawVal);
    IExpression *exponent = this->getExponentExpr(sourceFormat, targetFormat, rawVal);
    IExpression *sign = this->getSignExpr(sourceFormat, targetFormat, rawVal);
    return new BitwiseOr(sign, new BitwiseOr(exponent, significand));
  };

  IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) override {
    FloatFormat *sourceFormat = this->getTargetFloatFormat(compressedField);
    FloatFormat *targetFormat = this->resolveSourceFormat(compressedField);
    IExpression *significand = this->getSignificandExpr(sourceFormat, targetFormat, compressedVal);
    IExpression *exponent = this->getExponentExpr(sourceFormat, targetFormat, compressedVal);
    IExpression *sign = this->getSignExpr(sourceFormat, targetFormat, compressedVal);
    return new BitwiseOr(sign, new BitwiseOr(exponent, significand));
  };
};

class DelegatingFieldCompressor : public IFieldCompressor {
private:
  std::vector<IFieldCompressor *> compressors {
      new BoolFieldCompressor(),
      new IntegerRangeFieldCompressor(),
      new FloatFieldCompressor(),
      new EnumFieldCompressor()
  };

  IFieldCompressor *getSupportingCompressor(ClassField *field) {
    for (auto compressor : compressors) {
      if (compressor->isSupported(field)) return compressor;
    }
    return nullptr;
  }

public:
  bool isSupported(ClassField *classField) override {
    IFieldCompressor *compressor = getSupportingCompressor(classField);
    if (compressor == nullptr) return false;
    bool isSupported = compressor->isSupported(classField);
    return isSupported;
  }

  int getCompressedSize(ClassField *classField) override {
    return getSupportingCompressor(classField)->getCompressedSize(classField);
  }

  IExpression *getCompressedValExpr(ClassField *compressedField, IExpression *rawVal) override {
    return getSupportingCompressor(compressedField)->getCompressedValExpr(compressedField, rawVal);
  }

  IExpression *getDecompressedValExpr(ClassField *compressedField, IExpression *compressedVal) override {
    return getSupportingCompressor(compressedField)->getDecompressedValExpr(compressedField, compressedVal);
  }
};

#endif