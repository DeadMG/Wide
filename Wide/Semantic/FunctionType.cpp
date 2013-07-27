#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ClangTU.h>

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

Expression FunctionType::BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
    Expression out;
    out.t = ReturnType;
    if (Args.size() != args.size())
        throw std::runtime_error("Attempt to call the function with the wrong number of arguments.");
    // Our type system handles T vs T&& transparently, so substitution should be clean here. Just mention it in out.t.
    if (a.gen) {
        std::vector<Codegen::Expression*> e;
        // If the return type is complex, pass in pointer to result to be constructed, and mark our return type as an rvalue ref.
        if (out.t->IsComplexType()) {
            e.push_back(a.gen->CreateVariable(out.t->GetLLVMType(a), out.t->alignment(a)));
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
                e.push_back(Args[i]->BuildRvalueConstruction(args[i], a).Expr);
                continue;
            }

            // If we take value T, and the argument is T& or T&&, then just decay no problem.
            if (args[i].t->IsReference(Args[i])) {
                e.push_back(args[i].BuildValue(a).Expr);
                continue;
            }

            // If we take T&&, the only acceptable argument is T, in which case we need a copy, or U.
            if (auto rval = dynamic_cast<RvalueType*>(Args[i])) {
                // Since the types did not match, we know it can only be T, T&, or U. If T& error, else call BuildRvalueConstruction to construct a T&& from value T or U.
                if (auto lval = dynamic_cast<LvalueType*>(args[i].t)) {
                    // If T is the same, forbid.
                    if (rval->IsReference() == lval->IsReference()) {
                        throw std::runtime_error("Could not convert a T& to a T&&.");
                    }
                }

                // Try a copy. The user knows that unless inheritance is involved, we'll need a new value here anyway.
                // T::BuildRvalueConstruction called to construct a T in memory from T or some U, which may be reference.
                e.push_back(rval->IsReference()->BuildRvalueConstruction(args[i], a).Expr);
                continue;
            }

            // If we take T&, then the only acceptable target is T& or U. We already discarded T&, so go for U.
            // The only way this can work is if U inherits from T, or offers a UDC to T&, neither of which we support right now.
            // Except where Clang takes as const T& an rvalue, in which case we need to create an rvalue of U but pretend it's an lvalue.
            if (auto lval = dynamic_cast<LvalueType*>(Args[i])) {
                e.push_back(lval->IsReference()->BuildLvalueConstruction(args[i], a).Expr);
                continue;
            }

            // We take a value T, and we need to construct from some U. Use ValueConstruction.
            // Type::BuildValueConstruction called.
            e.push_back(Args[i]->BuildValueConstruction(args[i], a).Expr);
        }
        // Insert a bit cast because Clang sucks.
        out.Expr = a.gen->CreateFunctionCall(val.Expr, e, GetLLVMType(a));
        // If out's T is complex, then call f() and return e[0], which is the memory we allocated to store T, which is now constructed.
        // Also mark it for stealing
        if (out.t->IsComplexType()) {
            out.Expr = a.gen->CreateChainExpression(out.Expr, e[0]);
            out.t = a.GetRvalueType(out.t);
            out.steal = true;
        }
    }
    return out;
}
