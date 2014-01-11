#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> FunctionType::GetLLVMType(Analyzer& a) {
    std::function<llvm::Type*(llvm::Module*)> ret;
    std::vector<std::function<llvm::Type*(llvm::Module*)>> args;
    if (ReturnType->IsComplexType()) {
        ret = a.GetVoidType()->GetLLVMType(a);
        args.push_back(a.GetRvalueType(ReturnType)->GetLLVMType(a));
    } else {
        ret = ReturnType->GetLLVMType(a);
    }
    for(auto&& x : Args) {
        if (x->IsComplexType()) {
            args.push_back(a.GetRvalueType(x)->GetLLVMType(a));
        } else {
            args.push_back(x->GetLLVMType(a));
        }
    }
    return [=](llvm::Module* m) -> llvm::Type* {
        std::vector<llvm::Type*> types;
        for(auto x : args)
            types.push_back(x(m));
        return llvm::FunctionType::get(ret(m), types, false)->getPointerTo();
    };
}

clang::QualType FunctionType::GetClangType(ClangUtil::ClangTU& from, Analyzer& a) {
    std::vector<clang::QualType> types;
    for(auto x : Args)
        types.push_back(x->GetClangType(from, a));
    return from.GetASTContext().getFunctionType(ReturnType->GetClangType(from, a), types, clang::FunctionProtoType::ExtProtoInfo());
}

ConcreteExpression FunctionType::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    ConcreteExpression out(ReturnType, nullptr);
    if (Args.size() != args.size())
        throw std::runtime_error("Attempt to call the function with the wrong number of arguments.");
    // Our type system handles T vs T&& transparently, so substitution should be clean here. Just mention it in out.t.
    std::vector<Codegen::Expression*> e;
    // If the return type is complex, pass in pointer to result to be constructed, and mark our return type as an rvalue ref.
    if (out.t->IsComplexType()) {
        e.push_back(c->gen->CreateVariable(out.t->GetLLVMType(*c), out.t->alignment(*c)));
    }
    for(unsigned int i = 0; i < args.size(); ++i) {
        // If we take T, and the argument is T, then wahey.
        if (Args[i] == args[i].t) {
            e.push_back(args[i].Expr);
            continue;
        }

        // If T is complex and we take some U, then consider T as T&&.
        if (Args[i]->IsComplexType()) {
            // If T is complex, and we already have a reference, take that.
            if (args[i].t->IsReference(Args[i])) {
                e.push_back(args[i].Expr);
                continue;
            }
            // Else, we have a complex T from some U, in which case, perform rvalue construction.
            e.push_back(Args[i]->BuildRvalueConstruction(args[i], c).Expr);
            continue;
        }

        // If we take value T, and the argument is T& or T&&, then just decay no problem.
        if (args[i].t->IsReference(Args[i])) {
            e.push_back(args[i].BuildValue(c).Expr);
            continue;
        }

        // If we take T&&, the only acceptable argument is T, in which case we need a copy, or U.
        if (IsRvalueType(Args[i])) {
            // Since the types did not match, we know it can only be T, T&, or U. If T& error, else call BuildRvalueConstruction to construct a T&& from value T or U.
            if (IsLvalueType(args[i].t)) {
                // If T is the same, forbid.
                if (Args[i]->Decay() == args[i].t->Decay()) {
                    throw std::runtime_error("Could not convert a T& to a T&&.");
                }
            }

            // Try a copy. The user knows that unless inheritance is involved, we'll need a new value here anyway.
            // T::BuildRvalueConstruction called to construct a T in memory from T or some U, which may be reference.
            e.push_back(Args[i]->Decay()->BuildRvalueConstruction(args[i], c).Expr);
            continue;
        }

        // If we take T&, then the only acceptable target is T& or U. We already discarded T&, so go for U.
        // The only way this can work is if U inherits from T, or offers a UDC to T&, neither of which we support right now.
        // Except where Clang takes as const T& an rvalue, in which case we need to create an rvalue of U but pretend it's an lvalue.
        if (IsLvalueType(Args[i])) {
            e.push_back(Args[i]->Decay()->BuildLvalueConstruction(args[i], c).Expr);
            continue;
        }

        // We take a value T, and we need to construct from some U. Use ValueConstruction.
        // Type::BuildValueConstruction called.
        e.push_back(Args[i]->BuildValueConstruction(args[i], c).Expr);
    }
    // Insert a bit cast because Clang sucks.
    out.Expr = c->gen->CreateFunctionCall(val.Expr, e, GetLLVMType(*c));
    // If out's T is complex, then call f() and return e[0], which is the memory we allocated to store T, which is now constructed.
    // Also mark it for stealing
    if (out.t->IsComplexType()) {
        out.Expr = c->gen->CreateChainExpression(out.Expr, e[0]);
        out.t = c->GetRvalueType(out.t);
        out.steal = true;
    }
    return out;
}
