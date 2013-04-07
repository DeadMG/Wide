#include "OverloadSet.h"
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include "Function.h"
#include "FunctionType.h"
#include "../Parser/AST.h"
#include <array>

#pragma warning(push, 0)

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

OverloadSet::OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a) {
    for(auto x : s->functions)
        funcs.push_back(a.GetWideFunction(x));
    ty = std::make_shared<llvm::Type*>(nullptr);
}
std::function<llvm::Type*(llvm::Module*)> OverloadSet::GetLLVMType(Analyzer& a) {
    // Have to cache result - not fun.
    return [=](llvm::Module* m) -> llvm::Type* {
        if (*ty)
            return *ty;
        return llvm::StructType::get(m->getContext());
    };
}
Expression OverloadSet::BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) {
    std::vector<Function*> ViableCandidates;

    for(auto x : funcs) {
        if (x->GetSignature(a)->GetArguments().size() == args.size())
            ViableCandidates.push_back(x);
    }
    
    if (ViableCandidates.size() == 1) {
        auto call = ViableCandidates[0]->BuildCall(e, std::move(args), a);
        if (e.Expr)
            call.Expr = a.gen->CreateChainExpression(e.Expr, call.Expr);
        return call;
    }
    if (ViableCandidates.size() == 0)
        throw std::runtime_error("Attempted to call a function, but there were none with the right amount of arguments.");

    throw std::runtime_error("Attempted to call a function, but the call was ambiguous.");
}
Expression OverloadSet::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an overload set from more than one argument.");
    if (args.size() == 1) {
        if (args[0].t->IsReference(this))
            return args[0].t->BuildValue(args[0], a);
        if (args[0].t == this)
            return args[0];
        throw std::runtime_error("Can only construct overload set from another overload set of the same type.");
    }
    Expression out;
    out.t = this;
    out.Expr = a.gen->CreateNull(GetLLVMType(a));
    return out;
}