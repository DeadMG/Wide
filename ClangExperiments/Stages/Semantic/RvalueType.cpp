#include "RvalueType.h"
#include "ClangTU.h"
#include "Analyzer.h"
#include "LvalueType.h"
#include "../Codegen/Generator.h"
#include "../Codegen/Expression.h"

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> RvalueType::GetLLVMType(Analyzer& a) {
    auto f = GetPointee()->GetLLVMType(a);
    return [=](llvm::Module* m) {
        return f(m)->getPointerTo();
    };
}
clang::QualType RvalueType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getRValueReferenceType(GetPointee()->GetClangType(tu, a));
}

Codegen::Expression* RvalueType::BuildBooleanConversion(Expression e, Analyzer& a) {
    return Pointee->BuildBooleanConversion(e, a);
}
Expression RvalueType::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildEQComparison(lhs, rhs, a);
}
Expression RvalueType::BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildNEComparison(lhs, rhs, a);
}

Expression RvalueType::BuildValue(Expression lhs, Analyzer& analyzer) {
    Expression out;
    out.t = IsReference();
    if (analyzer.gen)
        out.Expr = analyzer.gen->CreateLoad(lhs.Expr);
    return out;
}


Expression RvalueType::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildRightShift(lhs, rhs, a);
}

Expression RvalueType::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLeftShift(lhs, rhs, a);
}
Expression RvalueType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTComparison(lhs, rhs, a);
}
Expression RvalueType::BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTEComparison(lhs, rhs, a);
}
Expression RvalueType::BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTComparison(lhs, rhs, a);
}
Expression RvalueType::BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTEComparison(lhs, rhs, a);
}