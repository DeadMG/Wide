#include "Bool.h"
#include "ClangTU.h"
#include "../Codegen/Generator.h"
#include "Analyzer.h"

#pragma warning(push, 0)

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Bool::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::Type::getInt8Ty(m->getContext());
    };
}

clang::QualType Bool::GetClangType(ClangUtil::ClangTU& where) {
    return where.GetASTContext().BoolTy;
}

Codegen::Expression* Bool::BuildBooleanConversion(Expression e, Analyzer& a) {
    if (e.t->IsReference(this))
        e = e.t->BuildValue(e, a);
    return e.Expr;
}

Expression Bool::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {    
    if (args.empty()) {
        Expression out;
        out.t = this;
        out.Expr = a.gen->CreateInt8Expression(0);
        return out;
    }

    // This function may be directly or indirectly invoked to construct from some other U. But currently we don't support that for primitives.
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct a bool from more than one argument.");

    // If args[0] is this, return it.
    if (args[0].t == this)
        return args[0];

    // If args[0] is T& or T&&, BuildValue().
    if (args[0].t->IsReference(this))
        return args[0].t->BuildValue(args[0], a);

    // No U conversions supported right now.
    throw std::runtime_error("Don't support constructing a bool from another type right now.");
}