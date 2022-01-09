#include "DataRepresentation.h"
#include "BitsizeDataTypeResolver.h"
#include "BitLevelInstructionSetResolver.h"
#include "FieldCompression.h"
#include "MemoryLayoutOptimiserResolver.h"
#include "GetterSetterGenerator.h"
#include "FieldCompression.h"
#include "Transformation.h"

class DataClassMemoryOptimiser : public ITransformation {
private:
    DelegatingFieldCompressor fieldCompressor;
    GetterSetterGenerator generator;
    BitsizeDataTypeResolver dataTypeResolver;
    BitLevelInstructionSetResolver bitLevelInstructionSetResolver;
    MemoryLayoutOptimiserResolver layoutOptimiserResolver;

    const std::string memoryOptAnnotation = "compressed";
    const std::string fieldIgnoreAnnotation = "ignored";
    const std::string tableName = "__table";
    const std::string compressedValName = "__compressed";
    const std::string fetchedCompressedValName = "__fetched";
    const std::string decompressedValName = "__decompressed";

    const std::string chunkSize = "chunk_size";

    bool isFieldIgnored(ClassField *classField) {
        for (auto annotation : classField->annotations) {
            if (annotation->tokens.empty()) continue;
            if (annotation->tokens[0] == fieldIgnoreAnnotation) return true;
        }
        return false;
    }

    bool isFieldToBeCompressed(ClassField *classField) {
        return this->fieldCompressor.isSupported(classField) && !this->isFieldIgnored(classField);
    }

    int getBlockSize(DataClass *sourceDataClass) {
        const std::set<char> supportedSizes {8, 16, 32, 64};
        for (auto annotation : sourceDataClass->annotations) {
            if (annotation->tokens[0] != chunkSize) continue;
            int size = std::stoi(annotation->tokens[1]);
            if (supportedSizes.find(size) == supportedSizes.end()) {
                std::cerr << "Unsupported chunk size! Supported sizes are 8, 16, 32, 64" <<  std::endl;
            }
            return size;
        }
        return 8;
    }

    DataClass *getEmptyCompressedDataClass(DataClass *sourceDataClass, PrimitiveDataType arrType, int arrSize) {
        DataClass *optimisedDataClass = new DataClass();
        optimisedDataClass->namespaces = sourceDataClass->namespaces;
        optimisedDataClass->superClasses = sourceDataClass->superClasses;
        optimisedDataClass->name = "Compressed" + sourceDataClass->name;
        ClassField *tableField = new ClassField();
        tableField->name = tableName;
        tableField->parentClass = optimisedDataClass;
        tableField->type = new ConstSizeArrayDataType(arrType, arrSize);
        tableField->accessModifier = AccessModifier::PRIVATE;
        optimisedDataClass->fields.emplace_back(tableField);
        for (auto field : sourceDataClass->fields) {
            if (this->isFieldToBeCompressed(field)) continue;
            ClassField *unprocessedField = field->copy();
            unprocessedField->accessModifier = AccessModifier::PRIVATE;
            unprocessedField->parentClass = optimisedDataClass;
            optimisedDataClass->fields.emplace_back(unprocessedField);
            optimisedDataClass->methods.emplace_back(generator.generateGetter(field));
            optimisedDataClass->methods.emplace_back(generator.generateSetter(field));
        }
        return optimisedDataClass;
    }

    IDataType *getCompressedBitsContainerDataType(CompressibleField *field) {
        int containerSize = 0;
        if (field->getSize() < 8) containerSize = 8;
        else containerSize = pow(2, ceil(log2(field->getSize())));
        PrimitiveDataType containerDataType = dataTypeResolver.resolveDataType(containerSize);
        return new PrimitiveData(containerDataType);
    }

    ClassMethod *generateCompressedGetter(CompressibleField *field, int blockSize, int offset) {
        ClassMethod *getter = generator.generateGetter(field->getClassField());
        getter->methodStatements.clear();
        IExpression *compressedVal = this->bitLevelInstructionSetResolver.resolveReadBits(
                new ReadBitsExpr(
                        new ClassFieldVariableAccess(new LocalVariableAccess(tableName)),
                        field->getSize(),
                        offset
                        ),
                blockSize
        );
        IExpression *getDecompressedVal = this->fieldCompressor.getDecompressedValExpr(field->getClassField(), new LocalVariableAccess(fetchedCompressedValName));

        int typeSize = dataTypeResolver.getSize(field->getClassField()->type);
        PrimitiveDataType containerType = dataTypeResolver.resolveDataType(typeSize);

        getter->methodStatements = std::vector<IStatement*> {
            new VariableDeclaration(new PrimitiveData(containerType), fetchedCompressedValName),
            new ValueAssignment(new LocalVariableAccess(fetchedCompressedValName), compressedVal),
            new VariableDeclaration(new PrimitiveData(containerType), decompressedValName),
            new ValueAssignment(new LocalVariableAccess(decompressedValName), getDecompressedVal),
            new ReturnStatement(new ReinterpretCast(new ReferenceDataType(field->getClassField()->type), new LocalVariableAccess(decompressedValName)))
        };
        return getter;
    }

    ClassMethod *generateCompressedSetter(CompressibleField *compressedField, int blockSize, int offset) {
        ClassMethod *setter = generator.generateSetter(compressedField->getClassField());
        IExpression *compressedVal = this->fieldCompressor.getCompressedValExpr(compressedField->getClassField(), new LocalVariableAccess(setter->methodArguments[0]->name));

        IDataType *compressedBitsStorageType = this->getCompressedBitsContainerDataType(compressedField);
        std::vector<IStatement *> setterStmts {
            new VariableDeclaration(compressedBitsStorageType, compressedValName),
            new ValueAssignment(new LocalVariableAccess(compressedValName), compressedVal)
        };
        std::vector<IStatement *> writterStmts = this->bitLevelInstructionSetResolver.resolveWriteBits(new WriteBitsStmt(
                new ReinterpretCast(new ReferenceDataType(new PrimitiveData(UNSIGNED_INT)), new LocalVariableAccess(compressedValName)),
                new ClassFieldVariableAccess(new LocalVariableAccess(tableName)),
                compressedField->getSize(),
                offset
        ), blockSize);
        setterStmts.insert(setterStmts.end(), writterStmts.begin(), writterStmts.end());
        setter->methodStatements = setterStmts;
        return setter;
    }

public:
    DataClass *getOptimisedClass(DataClass *sourceDataClass) {
        std::vector<CompressibleField *> compressibleFields;
        for (auto field : sourceDataClass->fields) {
            if (!this->isFieldToBeCompressed(field)) continue;
            compressibleFields.emplace_back(new CompressibleField(field, this->fieldCompressor.getCompressedSize(field)));
        }
        ILayoutOptimiser *layoutOptimiser;
        for (auto annotation : sourceDataClass->annotations) {
            if (annotation->tokens[0] != memoryOptAnnotation) continue;
            layoutOptimiser = this->layoutOptimiserResolver.resolve(annotation);
        }

        int blockSize = this->getBlockSize(sourceDataClass);
        MemoryMap *memoryMap = layoutOptimiser->getCompressedFields(compressibleFields, blockSize);
        PrimitiveDataType arrDataType = this->dataTypeResolver.resolveDataType(blockSize);
        DataClass *compressedDataClass = this->getEmptyCompressedDataClass(sourceDataClass, arrDataType, memoryMap->blockCount);
        int offset = 0;
        for (auto field : memoryMap->fields) {
            if (field->getClassField() != nullptr) {
                ClassMethod *getter = this->generateCompressedGetter(field, memoryMap->blockSize, offset);
                compressedDataClass->methods.emplace_back(getter);
                ClassMethod *setter = this->generateCompressedSetter(field, memoryMap->blockSize, offset);
                compressedDataClass->methods.emplace_back(setter);
            }
            offset += field->getSize();
        }
        return compressedDataClass;
    }

    bool supports(DataClass *dataClass) override {
        for (auto annotation : dataClass->annotations) {
            if (annotation->tokens[0] != memoryOptAnnotation) continue;
            return true;
        }
        return false;
    }

    std::vector<DataClass *> transform(DataClass *dataClass) override {
        DataClass *outputClass = this->getOptimisedClass(dataClass);
        return std::vector<DataClass *> {outputClass};
    }

};
