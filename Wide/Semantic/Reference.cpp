#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Reference::GetLLVMType(Analyzer& a) {
    auto f = IsReference()->GetLLVMType(a);
    return [=](llvm::Module* m) {
        return f(m)->getPointerTo();
    };
}
clang::QualType Reference::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getLValueReferenceType(Pointee->GetClangType(tu, a));
}
Expression Reference::BuildAssignment(Expression lhs, Expression rhs, Analyzer& analyzer) {
    return Pointee->BuildAssignment(lhs, rhs, analyzer);
}

Expression Reference::BuildValue(Expression lhs, Analyzer& analyzer) {
    Expression out;
    out.t = IsReference();
    if (analyzer.gen)
        out.Expr = analyzer.gen->CreateLoad(lhs.Expr);
    return out;
}

Expression Reference::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildRightShift(lhs, rhs, a);
}

Expression Reference::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLeftShift(lhs, rhs, a);
}
Codegen::Expression* Reference::BuildBooleanConversion(Expression e, Analyzer& a) {
    return Pointee->BuildBooleanConversion(e, a);
}
Expression Reference::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildEQComparison(lhs, rhs, a);
}
Expression Reference::BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildNEComparison(lhs, rhs, a);
}
Expression Reference::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTComparison(lhs, rhs, a);
}
Expression Reference::BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildLTEComparison(lhs, rhs, a);
}
Expression Reference::BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTComparison(lhs, rhs, a);
}
Expression Reference::BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    return Pointee->BuildGTEComparison(lhs, rhs, a);
}
Codegen::Expression* Reference::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return a.gen->CreateStore(mem, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}

Expression Reference::BuildDereference(Expression obj, Analyzer& a) {
    return Pointee->BuildDereference(obj, a);
}
Expression Reference::PointerAccessMember(Expression obj, std::string name, Analyzer& a) {
    return Pointee->PointerAccessMember(obj, name, a);
}
std::size_t Reference::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getPointerSize();
}
std::size_t Reference::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getPointerABIAlignment();
}

// Perform collapse
Expression Reference::BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return Expression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}
Expression Reference::BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return Expression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}