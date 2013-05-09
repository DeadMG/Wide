#include "LvalueType.h"
#include "ClangTU.h"
#include "Analyzer.h"
#include "../Codegen/Expression.h"
#include "../Codegen/Generator.h"
#include "RvalueType.h"

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> LvalueType::GetLLVMType(Analyzer& a) {
    auto f = IsReference()->GetLLVMType(a);
    return [=](llvm::Module* m) {
        return f(m)->getPointerTo();
    };
}
clang::QualType LvalueType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
   return tu.GetASTContext().getLValueReferenceType(Pointee->GetClangType(tu, a));
}
Expression LvalueType::BuildAssignment(Expression lhs, Expression rhs, Analyzer& analyzer) {
    return Pointee->BuildAssignment(lhs, rhs, analyzer);
}

Expression LvalueType::BuildValue(Expression lhs, Analyzer& analyzer) {
    Expression out;
    out.t = IsReference();
    if (analyzer.gen)
        out.Expr = analyzer.gen->CreateLoad(lhs.Expr);
    return out;
}

Expression LvalueType::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildRightShift(lhs, rhs, a);
}

Expression LvalueType::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLeftShift(lhs, rhs, a);
}
Codegen::Expression* LvalueType::BuildBooleanConversion(Expression e, Analyzer& a) {
    return Pointee->BuildBooleanConversion(e, a);
}
Expression LvalueType::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildEQComparison(lhs, rhs, a);
}
Expression LvalueType::BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildNEComparison(lhs, rhs, a);
}
Expression LvalueType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTComparison(lhs, rhs, a);
}
Expression LvalueType::BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTEComparison(lhs, rhs, a);
}
Expression LvalueType::BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTComparison(lhs, rhs, a);
}
Expression LvalueType::BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTEComparison(lhs, rhs, a);
}
Codegen::Expression* LvalueType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() > 1 || args[0].t != this)
        throw std::runtime_error("Cannot construct a reference from anything but another reference of the same type");
    return a.gen->CreateStore(mem, args[0].Expr);
}

Expression LvalueType::BuildDereference(Expression obj, Analyzer& a) {
    return Pointee->BuildDereference(obj, a);
}