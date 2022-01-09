#include <iostream>
#include <vector>
#include "LogicRepresentation.h"
#include "HighLevelFeatures.h"
#include "CppLanguageResolver.h"
#include "NestedBlockGenerator.h"
#include "llvm/Support/Casting.h"

class I {
protected:
    virtual std::string handleVariableAccess(IVariableAccess *a) { std::cerr << "Override me!" << std::endl; return std::string(); }
    virtual std::string handleExpression(IExpression *a) { std::cerr << "Override me!" << std::endl; return std::string(); }
};

class CppStatementGenerator : private I, private NestedBlockGenerator {
private:
    CppLanguageResolver languageResolver;

    std::string c(const std::string& val) {
        return val + ";";
    }

    std::string handleBitwiseAndAssignment(BitwiseAndAssignment *b) {
        return c(handleExpression(b->getVariable()) + " &= " + handleExpression(b->getValue()));
    }

    std::string handleBitwiseOrAssignment(BitwiseOrAssignment *b) {
        return c(handleExpression(b->getVariable()) + " |= " + handleExpression(b->getValue()));
    }

    std::string handleValueAssignment(ValueAssignment *v) {
        return c(handleExpression(v->getVariable()) + " = " + handleExpression(v->getValue()));
    }

    std::string handleListItemAssignment(ListItemAssignment *a) {
        ArrayVariableAccess *i = new ArrayVariableAccess(a->getVariable(), a->getIndex());
        ValueAssignment *v = new ValueAssignment(i, a->getValue());
        return handleValueAssignment(v);
    }

    std::string handleAssignStatement(IAssignStatement *a) {
        if (llvm::isa<BitwiseAndAssignment>(a)) {
            return handleBitwiseAndAssignment(reinterpret_cast<BitwiseAndAssignment *>(a));
        }
        if (llvm::isa<BitwiseOrAssignment>(a)) {
            return handleBitwiseOrAssignment(reinterpret_cast<BitwiseOrAssignment *>(a));
        }
        if (llvm::isa<ValueAssignment>(a)) {
            return handleValueAssignment(reinterpret_cast<ValueAssignment *>(a));
        }
        if (llvm::isa<ListItemAssignment>(a)) {
            return handleListItemAssignment(reinterpret_cast<ListItemAssignment *>(a));
        }
        std::cerr << "Unknown assign statement" << std::endl;
        return std::string();
    }

    std::vector<std::string> handleLoopStatement(LoopStatement *a) {
        std::vector<std::string> output;
        const std::string counterName = "__loop_counter_0";
        a->getIterCounter()->setVariableName(counterName);
        output.emplace_back(handleVariableDeclaration(new VariableDeclaration(new PrimitiveData(PrimitiveDataType::INT), counterName)));
        const std::string counterInitialAssignment = handleValueAssignment(new ValueAssignment(new LocalVariableAccess(counterName), new ConstExpr("0")));
        const std::string header = "for (" + counterInitialAssignment + " " + counterName + " < " + handleExpression(a->getIterCount()) + "; " + counterName + "++)";
        std::vector<std::string> innerStatements;
        for (IStatement *innerStatement : a->getInnerStatement()) {
            std::vector<std::string> innerStatementLines = handleStatement(innerStatement);
            innerStatements.insert(innerStatements.end(), innerStatementLines.begin(), innerStatementLines.end());
        }
        std::vector<std::string> loopStatementLines = this->getIndentedBlock(header, innerStatements, 0);
        output.insert(output.end(), loopStatementLines.begin(), loopStatementLines.end());
        return output;
    }

    std::vector<std::string> handleMultilineStatement(IMultilineStatement *a) {
        if (llvm::isa<LoopStatement>(a)) {
            return handleLoopStatement(reinterpret_cast<LoopStatement*>(a));
        }
        std::cerr << "Unknown multiline statement" << std::endl;
        return std::vector<std::string>();
    }

    std::string handleReturnStmt(ReturnStatement *r) {
        return c("return " + this->handleExpression(r->getReturnValue()));
    }

    std::string handleVariableDeclaration(VariableDeclaration *r) {
        return c(languageResolver.resolveType(r->getType()) + " " + r->getName());
    }

    std::string handleExpressionStatement(ExpressionStatement *s) {
        return c(handleExpression(s->getExpression()));
    }

    std::string handleThrowExceptionStatement(ThrowExceptionStmt *s) {
        return c("throw std::runtime_error(\"" + s->getMessage() + "\")");
    }

    std::string handleListLoadToArrayStatement(ListLoadToArray *s) {
        // done like so
        // std::copy(i.begin(), i.end(), i2);
        MethodInvocation *m = new MethodInvocation(new LocalVariableAccess("std::copy"), "", std::vector<IExpression *> {
            new MethodInvocation(s->getListAccess(), "begin", std::vector<IExpression *>()),
            new MethodInvocation(s->getListAccess(), "end", std::vector<IExpression *>()),
            s->getArrayAccess()
        });
        return c(handleMethodInvocation(m));
    }

    std::string handleListLoadFromArrayStatement(ListLoadFromArray *s) {
        // done like so
        // i.insert(i.begin(), &i2[0], &i2[2]);
        MethodInvocation *m = new MethodInvocation(s->getListAccess(), "insert", std::vector<IExpression *> {
                new MethodInvocation(s->getListAccess(), "begin", std::vector<IExpression *>()),
                new ConstExpr("&" + handleArrayVariableAccess(new ArrayVariableAccess(s->getArrayAccess(), new ConstExpr("0")))),
                new ConstExpr("&" + handleArrayVariableAccess(new ArrayVariableAccess(s->getArrayAccess(), s->getArrSize())))
        });
        return c(handleMethodInvocation(m));
    }

    std::string handleLiteralValue(LiteralValue *l) {
        return l->getLiteralValue();
    }

    std::string handleExpressionCast(ExpressionCast *e) {
        return "static_cast<" + languageResolver.resolveType(e->getTargetType()) + ">(" + handleExpression(e->getCastExpr()) + ")";
    }

    std::string handleReinterpretCast(ReinterpretCast *e) {
        return "reinterpret_cast<" + languageResolver.resolveType(e->getTargetType()) + ">(" + handleExpression(e->getCastExpr()) + ")";

    }

    std::string handleIsolatedEvaluation(IsolatedEvaluation *i) {
        return "(" + handleExpression(i->getProtectedExpression()) + ")";
    }

    std::string handleBitshiftLeft(BitshiftLeft *b) {
        return handleExpression(b->getBitshiftSubject()) + " << " + handleExpression(b->getBitshiftAmount());
    }

    std::string handleBitshiftRight(BitshiftRight *b) {
        return handleExpression(b->getBitshiftSubject()) + " >> " + handleExpression(b->getBitshiftAmount());
    }

    std::string handleAddition(Addition *a) {
        return handleExpression(a->getLeftExpression()) + " + " + handleExpression(a->getRightExpression());
    }

    std::string handleSubtraction(Subtraction *a) {
        return handleExpression(a->getLeftExpression()) + " - " + handleExpression(a->getRightExpression());
    }

    std::string handleEqualityComparison(EqualityComparison *a) {
        return handleExpression(a->getLeftExpression()) + " == " + handleExpression(a->getRightExpression());
    }

    std::string handleConditionalExpression(ConditionalExpression *a) {
        return handleExpression(a->getCondition()) + " ? " + handleExpression(a->getTrueVal()) + " : " + handleExpression(a->getFalseVal());
    }

    std::string handleBitwiseOr(BitwiseOr *b) {
        return handleExpression(b->getLeftExpression()) + " | " + handleExpression(b->getRightExpression());
    }

    std::string handleBitwiseAnd(BitwiseAnd *b) {
        return handleExpression(b->getLeftExpression()) + " & " + handleExpression(b->getRightExpression());
    }

    std::string handleArrayVariableAccess(ArrayVariableAccess *a) {
        return handleExpression(a->getArrayAccess()) + "[" + handleExpression(a->getIndex()) + "]";
    }

    std::string handleLocalVariableAccess(LocalVariableAccess *a) {
        return a->getVariableName();
    }

    std::string handleClassFieldVariableAccess(ClassFieldVariableAccess *a) {
        return "this->" + handleVariableAccess(a->getVariable());
    }

    std::string handleObjectFieldVariableAccess(ObjectFieldVariableAccess *a) {
        return handleVariableAccess(a->getObject()) + "." + a->getFieldName();
    }

    std::string handleListItemReadAccess(ListItemAccess *a) {
        return handleArrayVariableAccess(new ArrayVariableAccess(a->getListVariableAccess(), a->getIndex()));
    }

    std::string handleVariableAccess(IVariableAccess *a) override {
        if (llvm::isa<LocalVariableAccess>(a)) {
            return handleLocalVariableAccess(reinterpret_cast<LocalVariableAccess *>(a));
        }
        if (llvm::isa<ArrayVariableAccess>(a)) {
            return handleArrayVariableAccess(reinterpret_cast<ArrayVariableAccess *>(a));
        }
        if (llvm::isa<ClassFieldVariableAccess>(a)) {
            return handleClassFieldVariableAccess(reinterpret_cast<ClassFieldVariableAccess*>(a));
        }
        if (llvm::isa<ObjectFieldVariableAccess>(a)) {
            return handleObjectFieldVariableAccess(reinterpret_cast<ObjectFieldVariableAccess*>(a));
        }
        if (llvm::isa<ListItemAccess>(a)) {
            return handleListItemReadAccess(reinterpret_cast<ListItemAccess *>(a));
        }
        std::cerr << "Unknown variable access" << std::endl;
        return "unknown_variable_access";
    }

    std::string handleMethodInvocation(MethodInvocation *a) {
        std::string name = a->getMethodName().length() > 0 ? "." + a->getMethodName() : "";
        std::string methodInvocation = handleVariableAccess(a->getTarget()) + name  + "(";
        for (auto arg: a->getArgs()) {
            methodInvocation += handleExpression(arg) + ", ";
        }
        if (!a->getArgs().empty()) {
            methodInvocation.pop_back();
            methodInvocation.pop_back(); //removing the trailing ', '
        }
        methodInvocation += ")";
        return methodInvocation;
    }

    std::string handleClassInstanceMethodInvocation(ClassInstanceMethodInvocation *a) {
        std::string methodInvocation = "this->" + a->getMethodName() + "(";
        for (auto arg: a->getArgs()) {
            methodInvocation += handleExpression(arg) + ", ";
        }
        if (!a->getArgs().empty()) {
            methodInvocation.pop_back();
            methodInvocation.pop_back(); //removing the trailing ', '
        }
        methodInvocation += ")";
        return methodInvocation;
    }

    std::string handleConstExpression(ConstExpr *a) {
        return a->getValue();
    }

    std::string handleListGetSize(ListGetSize *a) {
        MethodInvocation *i = new MethodInvocation(a->getListAccess(), "size", std::vector<IExpression*>());
        return this->handleMethodInvocation(i);
    }

    std::string handleExpression(IExpression *a) override {
        if (a == nullptr) {
            return "";
        }
        if (llvm::isa<IVariableAccess>(a)) {
            return handleVariableAccess(reinterpret_cast<IVariableAccess*>(a));
        }
        if (llvm::isa<BitwiseAnd>(a)) {
            return handleBitwiseAnd(reinterpret_cast<BitwiseAnd *>(a));
        }
        if (llvm::isa<BitwiseOr>(a)) {
            return handleBitwiseOr(reinterpret_cast<BitwiseOr *>(a));
        }
        if (llvm::isa<BitshiftRight>(a)) {
            return handleBitshiftRight(reinterpret_cast<BitshiftRight *>(a));
        }
        if (llvm::isa<BitshiftLeft>(a)) {
            return handleBitshiftLeft(reinterpret_cast<BitshiftLeft *>(a));
        }
        if (llvm::isa<Addition>(a)) {
            return handleAddition(reinterpret_cast<Addition*>(a));
        }
        if (llvm::isa<Subtraction>(a)) {
            return handleSubtraction(reinterpret_cast<Subtraction*>(a));
        }
        if (llvm::isa<EqualityComparison>(a)) {
            return handleEqualityComparison(reinterpret_cast<EqualityComparison*>(a));
        }
        if (llvm::isa<ConditionalExpression>(a)) {
            return handleConditionalExpression(reinterpret_cast<ConditionalExpression*>(a));
        }
        if (llvm::isa<IsolatedEvaluation>(a)) {
            return handleIsolatedEvaluation(reinterpret_cast<IsolatedEvaluation *>(a));
        }
        if (llvm::isa<ExpressionCast>(a)) {
            return handleExpressionCast(reinterpret_cast<ExpressionCast *>(a));
        }
        if (llvm::isa<ReinterpretCast >(a)) {
            return handleReinterpretCast(reinterpret_cast<ReinterpretCast *>(a));
        }
        if (llvm::isa<LiteralValue>(a)) {
            return handleLiteralValue(reinterpret_cast<LiteralValue *>(a));
        }
        if (llvm::isa<MethodInvocation>(a)) {
            return handleMethodInvocation(reinterpret_cast<MethodInvocation *>(a));
        }
        if (llvm::isa<ClassInstanceMethodInvocation>(a)) {
            return handleClassInstanceMethodInvocation(reinterpret_cast<ClassInstanceMethodInvocation *>(a));
        }
        if (llvm::isa<ConstExpr>(a)) {
            return handleConstExpression(reinterpret_cast<ConstExpr*>(a));
        }
        if (llvm::isa<ListGetSize>(a)) {
            return handleListGetSize(reinterpret_cast<ListGetSize *>(a));
        }
        std::cerr << "Unknown expression" << std::endl;
        return std::string();;
    }

public:
    std::vector<std::string> handleStatement(IStatement *s) {
        std::string output;
        if (llvm::isa<ReturnStatement>(s)) {
            output = handleReturnStmt(reinterpret_cast<ReturnStatement *>(s));
        }
        else if (llvm::isa<VariableDeclaration>(s)) {
            output = handleVariableDeclaration(reinterpret_cast<VariableDeclaration *>(s));
        }
        else if (llvm::isa<IAssignStatement>(s)) {
            output = handleAssignStatement(reinterpret_cast<IAssignStatement *>(s));
        }
        else if (llvm::isa<ExpressionStatement>(s)) {
            output = handleExpressionStatement(reinterpret_cast<ExpressionStatement*>(s));
        }
        else if (llvm::isa<ThrowExceptionStmt>(s)) {
            output = handleThrowExceptionStatement(reinterpret_cast<ThrowExceptionStmt*>(s));
        }
        else if (llvm::isa<ListLoadToArray>(s)) {
            output = handleListLoadToArrayStatement(reinterpret_cast<ListLoadToArray*>(s));
        }
        else if (llvm::isa<ListLoadFromArray>(s)) {
            output = handleListLoadFromArrayStatement(reinterpret_cast<ListLoadFromArray*>(s));
        }
        if (output.length() != 0) return std::vector<std::string> { output };
        if (llvm::isa<IMultilineStatement>(s)) {
            return handleMultilineStatement(reinterpret_cast<IMultilineStatement*>(s));
        }
        std::cerr << "Unknown statement" << std::endl;
        return std::vector<std::string>();
    }

    std::vector<std::string> handleStatements(const std::vector<IStatement*>& statements) {
        std::vector<std::string> outputStatements;
        outputStatements.reserve(statements.size());
        for (auto statement : statements) {
            const std::vector<std::string> lines = handleStatement(statement);
            outputStatements.insert(outputStatements.end(), lines.begin(), lines.end());
        }
        return outputStatements;
    }
};
