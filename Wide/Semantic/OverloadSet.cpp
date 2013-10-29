#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <array>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <clang/Sema/Sema.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

template<typename T, typename U> T* debug_cast(U* other) {
    assert(dynamic_cast<T*>(other));
    return static_cast<T*>(other);
}

OverloadSet::OverloadSet(const AST::FunctionOverloadSet* s, Type* mem) {
    for(auto x : s->functions)
        functions[mem].insert(x);
}
OverloadSet::OverloadSet(std::unordered_set<const AST::Function*> funcs, Type* mem) 
{
    if (funcs.size() != 0)
        functions[mem].insert(funcs.begin(), funcs.end());
}
Type* OverloadSet::nonstatic() {
    for(auto&& x : functions)
        if (auto udt = dynamic_cast<UserDefinedType*>(x.first->Decay()))
            return udt;
    return nullptr;
}
std::function<llvm::Type*(llvm::Module*)> OverloadSet::GetLLVMType(Analyzer& a) {
    // Have to cache result - not fun.
    auto g = a.gen;
    std::stringstream stream;
    stream << "struct.__" << this;
    auto str = stream.str();
    if (!nonstatic()) {
        return [=](llvm::Module* m) -> llvm::Type* {
            llvm::Type* t = nullptr;
            if (m->getTypeByName(str))
                t = m->getTypeByName(str);
            else {
                auto int8ty = llvm::IntegerType::getInt8Ty(m->getContext());
                t = llvm::StructType::create(str, int8ty, nullptr);
            }
            g->AddEliminateType(t);
            return t;
        };
    }
    auto ty = nonstatic()->Decay()->GetLLVMType(a);
    return [=](llvm::Module* m) -> llvm::Type* {
        llvm::Type* t = nullptr;
        if (m->getTypeByName(str))
            t = m->getTypeByName(str);
        else {
            t = llvm::StructType::create(str, ty(m)->getPointerTo(), nullptr);
        }
        return t;
    };    
}
Expression OverloadSet::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    std::vector<Type*> targs;
    for(auto x : args)
        targs.push_back(x.t);
    auto call = Resolve(std::move(targs), a);
    if (!call)
        throw std::runtime_error("Fuck!");

    if (auto func = dynamic_cast<Function*>(call))
        if (dynamic_cast<UserDefinedType*>(func->GetContext(a)->Decay()))
            args.insert(args.begin(), ConcreteExpression(func->GetContext(a), a.gen->CreateFieldExpression(e.BuildValue(a, where).Expr, 0)));

    return call->BuildCall(ConcreteExpression(), std::move(args), a, where);
}

OverloadSet::OverloadSet(std::unordered_set<Callable*> call)
    : callables(std::move(call)) {}

Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Analyzer& a) {
    std::vector<std::pair<Type*, const AST::Function*>> ViableCandidates;
    for(auto&& x : functions)
        for(auto func : x.second)
            ViableCandidates.emplace_back(x.first, func);
    auto is_compatible = [](Type* takety, Type* argty) -> bool {
        if (takety->Decay() != argty->Decay())
            return false;

        //      argument:  T       T&        T&&
        // takes:
        //   T           accept IsCopyable IsMovable
        //  T&           reject   accept    reject
        // T&&           accept   reject    accept
        // T T can only come up if T is primitive, so we know in advance if T is copyable or not.
          
        if (takety == argty)
            return true;
        if (IsRvalueType(takety)) {
            if (IsLvalueType(argty))
                return false;
            return true;
        }
        if (IsLvalueType(takety)) {
            // Already accepted T& T&, and both other cases are rejection.
            return false;
        }
        if (IsLvalueType(argty))
            if (takety->IsCopyable())
                return true;
        if (IsRvalueType(argty))
            if (takety->IsMovable())
                return true;
        // Should have covered all nine cases here.
        __debugbreak();
    };
    ViableCandidates.erase(
        std::remove_if(ViableCandidates.begin(), ViableCandidates.end(), [&](std::pair<Type*, const AST::Function*> candidate) {
            auto args = f_args;
            if (candidate.second->args.size() >= 1)
                if (candidate.second->args[0].name == "this")
                    args.insert(args.begin(), candidate.first);
            if (args.size() != candidate.second->args.size())
                return true;
            for(std::size_t i = 0; i < args.size(); ++i) {
                if (!candidate.second->args[i].type)
                    continue;
                auto takety = dynamic_cast<ConstructorType*>(a.AnalyzeExpression(candidate.first, candidate.second->args[i].type).Resolve(nullptr).t->Decay());
                if (!takety)
                    throw std::runtime_error("Fuck");
                // We don't accept any U here right now.
                if (is_compatible(takety->GetConstructedType(), args[i]))
                    continue;
                return true;
            }
            return false;
        }), 
        ViableCandidates.end()
    );
    auto call = callables;
    for(auto it = call.begin(); it != call.end();) {
        auto funcobj = *it;
        auto args = funcobj->GetArgumentTypes(a);
        if (args.size() != f_args.size()) {
            it = call.erase(it);
            continue;
        }
        for(std::size_t i = 0; i < args.size(); ++i) {
            if (!args[i])
                continue;
            if (is_compatible(args[i], f_args[i]))
                continue;
            it = call.erase(it);
            break;
        }
        if (it != call.end()) ++it;
    }
    
    if (ViableCandidates.size() + call.size() != 1)
        return nullptr;
    if (ViableCandidates.size() == 1)
        return a.GetWideFunction(ViableCandidates[0].second, ViableCandidates[0].first, f_args);
    return *call.begin();
}

Codegen::Expression* OverloadSet::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an overload set from more than one argument.");
    if (args.size() == 1) {
        if (args[0].BuildValue(a, where).t == this)
            return a.gen->CreateStore(mem, args[0].BuildValue(a, where).Expr);
        if (nonstatic()) {
            if (args[0].t->IsReference(nonstatic()) || (nonstatic()->IsReference() && nonstatic() == args[0].t))
                return a.gen->CreateStore(a.gen->CreateFieldExpression(mem, 0), args[0].Expr);
            if (args[0].t == nonstatic())
                assert("Internal compiler error: Attempt to call a member function of a value.");
        }
        throw std::runtime_error("Can only construct overload set from another overload set of the same type, or a reference to T.");
    }
    if (nonstatic())
        throw std::runtime_error("Cannot default-construct a non-static overload set.");
    return a.gen->CreateNull(GetLLVMType(a));
}
clang::QualType OverloadSet::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];

    std::stringstream stream;
    stream << "__" << this;
    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    if (nonstatic()) {
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo("__this"),
            TU.GetASTContext().getPointerType(nonstatic()->Decay()->GetClangType(TU, a)),
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
        );
        var->setAccess(clang::AccessSpecifier::AS_public);
        recdecl->addDecl(var);
    }
    for(auto&& pair : functions) {
        // Instead of this, they will take a pointer to the recdecl here.
        // Wide member function types take "this" into account, whereas Clang ones do not.
        for(auto&& x : pair.second) {
            for(auto arg : x->args) if (!arg.type) continue;
            auto f = a.GetWideFunction(x, pair.first);
            auto sig = f->GetSignature(a);
            if (nonstatic()) {
                auto ret = sig->GetReturnType();
                auto args = sig->GetArguments();
                args.erase(args.begin());
                sig = a.GetFunctionType(ret, args);
            }
            auto meth = clang::CXXMethodDecl::Create(
                TU.GetASTContext(), 
                recdecl, 
                clang::SourceLocation(), 
                clang::DeclarationNameInfo(TU.GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Call), clang::SourceLocation()),
                sig->GetClangType(TU, a),
                0,
                clang::FunctionDecl::StorageClass::SC_Extern,
                false,
                false,
                clang::SourceLocation()
            );        
            assert(!meth->isStatic());
            meth->setAccess(clang::AccessSpecifier::AS_public);
            std::vector<clang::ParmVarDecl*> decls;
            for(auto&& arg : sig->GetArguments()) {
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
                auto trampoline = a.gen->CreateFunction([=, &a, &TU](llvm::Module* m) -> llvm::Type* {
                    auto fty = llvm::dyn_cast<llvm::FunctionType>(sig->GetLLVMType(a)(m)->getPointerElementType());
                    std::vector<llvm::Type*> args;
                    for(auto it = fty->param_begin(); it != fty->param_end(); ++it) {
                        args.push_back(*it);
                    }
                    // If T is complex, then "this" is the second argument. Else it is the first.
                    auto self = TU.GetLLVMTypeFromClangType(TU.GetASTContext().getTypeDeclType(recdecl), a)(m)->getPointerTo();
                    if (sig->GetReturnType()->IsComplexType()) {
                        args.insert(args.begin() + 1, self);
                    } else {
                        args.insert(args.begin(), self);
                    }
                    return llvm::FunctionType::get(fty->getReturnType(), args, false)->getPointerTo();
                }, TU.MangleName(meth), nullptr, true);// If an i8/i1 mismatch, fix it up for us.
                // The only statement is return f().
                std::vector<Codegen::Expression*> exprs;
                // Fucking ABI putting complex return type parameter first before this.
                // It's either "All except the first" or "All except the second", depending on whether or not the return type is complex.
                if (sig->GetReturnType()->IsComplexType()) {
                    // Two hidden arguments: ret, this, skip this and do the rest.
                    // If we are nonstatic, then perform a load.
                    exprs.push_back(a.gen->CreateParameterExpression(0));
                    if (nonstatic())
                        exprs.push_back(a.gen->CreateFieldExpression(a.gen->CreateParameterExpression(1), 0));
                    for(std::size_t i = 2; i < sig->GetArguments().size() + 2; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                } else {
                    // One hidden argument: this, pos 0. Skip it and do the rest.
                    if (nonstatic())
                        exprs.push_back(a.gen->CreateLoad(a.gen->CreateFieldExpression(a.gen->CreateParameterExpression(0), 0)));
                    for(std::size_t i = 1; i < sig->GetArguments().size() + 1; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                }
                trampoline->AddStatement(a.gen->CreateReturn(a.gen->CreateFunctionCall(a.gen->CreateFunctionValue(f->GetName()), exprs, f->GetLLVMType(a))));
            }
        }
    }
    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
}
std::size_t OverloadSet::size(Analyzer& a) {
    if (!nonstatic()) return a.gen->GetInt8AllocSize();
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t OverloadSet::alignment(Analyzer& a) {
    if (!nonstatic()) return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8);
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}
OverloadSet::OverloadSet(OverloadSet* s, OverloadSet* other) {
    for(auto x : s->functions)
        functions.insert(x);
    for(auto x : other->functions)
        functions.insert(x);
    for(auto x : s->callables)
        callables.insert(x);
    for(auto x : other->callables)
        callables.insert(x);
}