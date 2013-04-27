#include "OverloadSet.h"
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include "Function.h"
#include "FunctionType.h"
#include "../Parser/AST.h"
#include "ClangTU.h"
#include <array>
#include <sstream>
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include "../Codegen/Function.h"

#pragma warning(push, 0)

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

OverloadSet::OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a) {
    for(auto x : s->functions)
        funcs.push_back(a.GetWideFunction(x));
}
std::function<llvm::Type*(llvm::Module*)> OverloadSet::GetLLVMType(Analyzer& a) {
    // Have to cache result - not fun.
    auto g = a.gen;
    return [=](llvm::Module* m) -> llvm::Type* {
        std::stringstream stream;
        stream << "class.__" << this;
        llvm::Type* t = nullptr;
        if (m->getTypeByName(stream.str()))
            t = m->getTypeByName(stream.str());
        else {
            auto int8ty = llvm::IntegerType::getInt8Ty(m->getContext());
            t = llvm::StructType::create(stream.str(), int8ty, nullptr);
        }
        g->AddEliminateType(t);
        return t;
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
clang::QualType OverloadSet::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];

    std::stringstream stream;
    stream << "__" << this;
    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    for(auto&& f : funcs) {
        auto meth = clang::CXXMethodDecl::Create(
            TU.GetASTContext(), 
            recdecl, 
            clang::SourceLocation(), 
            clang::DeclarationNameInfo(TU.GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Call), clang::SourceLocation()),
            f->GetClangType(TU, a),
            0,
            clang::FunctionDecl::StorageClass::SC_Extern,
            false,
            false,
            clang::SourceLocation()
        );        
        assert(!meth->isStatic());
        meth->setAccess(clang::AccessSpecifier::AS_public);
        std::vector<clang::ParmVarDecl*> decls;
        for(auto&& arg : f->GetSignature(a)->GetArguments()) {
            decls.push_back(clang::ParmVarDecl::Create(TU.GetASTContext(),
                meth,
                clang::SourceLocation(),
                clang::SourceLocation(),
                nullptr,
                arg->GetClangType(TU, a),
                nullptr,
                clang::VarDecl::StorageClass::SC_Auto,
                nullptr
            ));
        }
        meth->setParams(decls);
        recdecl->addDecl(meth);
        // If this is the first time, then we need to define the trampolines.
        if (clangtypes.empty()) {
            auto sig = f->GetSignature(a);
            auto trampoline = a.gen->CreateFunction([=, &a, &TU](llvm::Module* m) -> llvm::Type* {
                auto fty = llvm::dyn_cast<llvm::FunctionType>(sig->GetLLVMType(a)(m)->getPointerElementType());
                std::vector<llvm::Type*> args;
                for(auto it = fty->param_begin(); it != fty->param_end(); ++it) {
                    args.push_back(*it);
                }
				// If T is complex, then "this" is the second argument. Else it is the first.

                auto self = TU.GetLLVMTypeFromClangType(TU.GetASTContext().getTypeDeclType(recdecl))(m)->getPointerTo();
				if (sig->GetReturnType()->IsComplexType()) {
					args.insert(args.begin() + 1, self);
				} else {
					args.insert(args.begin(), self);
				}
                return llvm::FunctionType::get(fty->getReturnType(), args, false)->getPointerTo();
            }, TU.MangleName(meth), true);// If an i8/i1 mismatch, fix it up for us.
            // The only statement is return f().
            std::vector<Codegen::Expression*> exprs;
            // Fucking ABI putting complex return type parameter first before this.
            // It's either "All except the first" or "All except the second", depending on whether or not the return type is complex.
            if (sig->GetReturnType()->IsComplexType()) {
                // Two hidden arguments: ret, this, skip this and do the rest.
                exprs.push_back(a.gen->CreateParameterExpression(0));
                for(std::size_t i = 2; i < sig->GetArguments().size() + 2; ++i) {
                    exprs.push_back(a.gen->CreateParameterExpression(i));
                }
            } else {
                // One hidden argument: this, pos 0. Skip it and do the rest.
                for(std::size_t i = 1; i < sig->GetArguments().size() + 1; ++i) {
                    exprs.push_back(a.gen->CreateParameterExpression(i));
                }
            }
            trampoline->AddStatement(a.gen->CreateReturn(a.gen->CreateFunctionCall(a.gen->CreateFunctionValue(f->GetName()), exprs)));
        }
    }
    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
}