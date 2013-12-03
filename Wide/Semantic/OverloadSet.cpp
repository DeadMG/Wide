#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Util/DebugUtilities.h>
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
#include <llvm/IR/DataLayout.h>
#include <clang/Sema/Overload.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

template<typename T, typename U> T* debug_cast(U* other) {
    assert(dynamic_cast<T*>(other));
    return static_cast<T*>(other);
}

OverloadSet::OverloadSet(const AST::FunctionOverloadSet* s, Type* mem) : from(nullptr), nonstatic(nullptr) {
    if (dynamic_cast<UserDefinedType*>(mem->Decay()))
        nonstatic = mem;
    for(auto x : s->functions)
        functions[mem].insert(x);
}
OverloadSet::OverloadSet(std::unordered_set<const AST::Function*> funcs, Type* mem) : from(nullptr), nonstatic(nullptr)
{
    if (funcs.size() != 0)
        functions[mem].insert(funcs.begin(), funcs.end());
    if (dynamic_cast<UserDefinedType*>(mem->Decay()))
        nonstatic = mem;
}
std::function<llvm::Type*(llvm::Module*)> OverloadSet::GetLLVMType(Analyzer& a) {
    // Have to cache result - not fun.
    auto g = a.gen;
    std::stringstream stream;
    stream << "struct.__" << this;
    auto str = stream.str();
    if (!nonstatic) {
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
    auto ty = nonstatic->Decay()->GetLLVMType(a);
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
Expression OverloadSet::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
    std::vector<Type*> targs;
    if (nonstatic)
        targs.push_back(nonstatic);
    for(auto x : args)
        targs.push_back(x.t);
    auto call = Resolve(std::move(targs), *c);
    if (!call)
        throw std::runtime_error("Fuck!");

    if (call->AddThis())
        args.insert(args.begin(), ConcreteExpression(nonstatic, c->gen->CreateFieldExpression(e.BuildValue(c).Expr, 0)));

    return call->BuildCall(call->BuildValueConstruction(c), std::move(args), c);
}

OverloadSet::OverloadSet(std::unordered_set<Callable*> call)
    : callables(std::move(call)), from(nullptr), nonstatic(nullptr) {}

Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Analyzer& a) {
    std::vector<std::pair<Type*, const AST::Function*>> ViableCandidates;
    for(auto&& x : functions)
        for(auto func : x.second)
            ViableCandidates.emplace_back(x.first, func);
    ViableCandidates.erase(
        std::remove_if(ViableCandidates.begin(), ViableCandidates.end(), [&](std::pair<Type*, const AST::Function*> candidate) {
            auto args = f_args;
            // If a this was added, and we're not in a context that calls for resolving it, then erase it from the list.
            if (nonstatic) {
                if (candidate.second->args.size() == 0 || candidate.second->args[0].name != "this" || !dynamic_cast<UserDefinedType*>(candidate.first->Decay())) {
                    args.erase(args.begin());
                }
            }
            if (args.size() != candidate.second->args.size())
                return true;
            struct OverloadSetLookupContext : public MetaType {
                Type* context;
                Type* member;
                Type* argument;
                Wide::Util::optional<Expression> AccessMember(ConcreteExpression, std::string name, Context c) override final {
                    if(name == "this") {
                        if(member)
                            return c->GetConstructorType(member)->BuildValueConstruction(c);
                        throw std::runtime_error("Attempt to access this in a non-member.");
                    }
                    if(name == "auto")
                        return c->GetConstructorType(argument)->BuildValueConstruction(c);
                    return Wide::Util::none;
                }
                Type* GetContext(Analyzer& a) override final {
                    return context;
                }
            };
            for(std::size_t i = 0; i < args.size(); ++i) {
                OverloadSetLookupContext lc;
                lc.argument = args[i];
                lc.context = candidate.first;
                lc.member = dynamic_cast<UserDefinedType*>(candidate.first->Decay()) ? candidate.first->Decay() : nullptr;
                auto takety = candidate.second->args[i].type ? 
                    dynamic_cast<ConstructorType*>(a.AnalyzeExpression(&lc, candidate.second->args[i].type, [](ConcreteExpression e) {}).Resolve(nullptr).t->Decay()) :
                    a.GetConstructorType(args[i]->Decay());
                if (!takety)
                    throw Wide::Semantic::SemanticError(candidate.second->args[i].location, Wide::Semantic::Error::ExpressionNoType);
                // We don't accept any U here right now.
                if (args[i]->IsA(takety->GetConstructedType()))
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
            auto ty = args[i] ? args[i] : f_args[i]->Decay();
            if (f_args[i]->IsA(ty))
                continue;
            it = call.erase(it);
            break;
        }
        if (it != call.end()) ++it;
    }

    auto get_wide_or_result = [&]() -> Callable* {
        if (ViableCandidates.size() + call.size() != 1)
            return nullptr;
        if (ViableCandidates.size() == 1)
            return a.GetWideFunction(ViableCandidates[0].second, ViableCandidates[0].first, f_args);
        return *call.begin();
    };

    if (from) {    
        std::vector<clang::OpaqueValueExpr> exprs;
        exprs.reserve(f_args.size() + 1);
        for(auto x : f_args)
            exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x)));
        std::vector<clang::Expr*> exprptrs;
        for(auto&& x : exprs)
            exprptrs.push_back(&x);
        clang::OverloadCandidateSet s((clang::SourceLocation()));
        clang::UnresolvedSet<8> us;
        for(auto decl : clangfuncs)
            if (!decl->isCXXInstanceMember())
                us.addDecl(decl);
        from->GetSema().AddFunctionCandidates(us, exprptrs, s, false, nullptr);

        if (nonstatic) {
            us.clear();
            for(auto decl : clangfuncs)
                if (decl->isCXXInstanceMember())
                    us.addDecl(decl);
            //exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), nonstatic->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(nonstatic)));
            //exprptrs.push_back(&exprs.back());
            from->GetSema().AddFunctionCandidates(us, exprptrs, s, false, nullptr);
        }
        assert(s.size() == clangfuncs.size());
        clang::OverloadCandidateSet::iterator best;
        auto result = s.BestViableFunction(from->GetSema(), clang::SourceLocation(), best);
        if (result != clang::OverloadingResult::OR_Success) {
            //s.NoteCandidates(from->GetSema(), clang::OverloadCandidateDisplayKind::OCD_AllCandidates, exprptrs);
            return get_wide_or_result();
        }
        auto wide_result = get_wide_or_result();
        if (wide_result)
            throw std::runtime_error("Attempted to resolve an overload set, but both Wide and C++ provided viable results.");
        auto fun = best->Function;
        struct cppcallable : public Callable, public MetaType {
            clang::FunctionDecl* fun;
            ClangUtil::ClangTU* from;
            Type* nonstatic;
            std::vector<Type*> GetArgumentTypes(Analyzer& a) override {
                std::vector<Type*> types;
                if (AddThis() && nonstatic)
                    types.push_back(nonstatic);
                for(unsigned i = 0; i < fun->getNumParams(); ++i)
                    types.push_back(a.GetClangType(*from, fun->getParamDecl(i)->getType()));
                return types;
            }
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override {
                return ConcreteExpression(c->GetFunctionType(c->GetClangType(*from, fun->getResultType()), GetArgumentTypes(*c)), c->gen->CreateFunctionValue(from->MangleName(fun))).BuildCall(args, c);
            }
            bool AddThis() override {
                return fun->isCXXInstanceMember();
            }
        };
        auto p = new cppcallable();
        p->fun = fun;
        p->nonstatic = nonstatic;
        p->from = from;
        return p;
    }
    return get_wide_or_result();
}

Codegen::Expression* OverloadSet::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) {
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an overload set from more than one argument.");
    if (args.size() == 1) {
        if (args[0].BuildValue(c).t == this)
            return c->gen->CreateStore(mem, args[0].BuildValue(c).Expr);
        if (nonstatic) {
            if (args[0].t->IsReference(nonstatic) || (nonstatic->IsReference() && nonstatic == args[0].t))
                return c->gen->CreateStore(c->gen->CreateFieldExpression(mem, 0), args[0].Expr);
            if (args[0].t == nonstatic)
                assert("Internal compiler error: Attempt to call a member function of a value.");
        }
        throw std::runtime_error("Can only construct overload set from another overload set of the same type, or a reference to T.");
    }
    if (nonstatic)
        throw std::runtime_error("Cannot default-construct a non-static overload set.");
    return c->gen->CreateNull(GetLLVMType(*c));
}
clang::QualType OverloadSet::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];

    std::stringstream stream;
    stream << "__" << this;
    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    if (nonstatic) {
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo("__this"),
            TU.GetASTContext().getPointerType(nonstatic->Decay()->GetClangType(TU, a)),
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
            if (nonstatic) {
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
                    if (nonstatic)
                        exprs.push_back(a.gen->CreateFieldExpression(a.gen->CreateParameterExpression(1), 0));
                    for(std::size_t i = 2; i < sig->GetArguments().size() + 2; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                } else {
                    // One hidden argument: this, pos 0. Skip it and do the rest.
                    if (nonstatic)
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
    if (!nonstatic) return a.gen->GetInt8AllocSize();
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t OverloadSet::alignment(Analyzer& a) {
    if (!nonstatic) return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8);
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
    for(auto x : other->clangfuncs)
        clangfuncs.insert(x);
    clangfuncs.insert(s->clangfuncs.begin(), s->clangfuncs.end());
    if (s->from && other->from && s->from != other->from)
        assert(false && "Attempted to combine an overload set of two overload sets containing functions from two different Clang TUs.");
    from = s->from;
    if (!from)
        from = other->from;
    if (s->nonstatic && other->nonstatic && s->nonstatic != other->nonstatic)
        assert(false && "Attempted to combine an overload set of two overload sets containing functions which are members of two different types.");
    nonstatic = s->nonstatic;
    if (!nonstatic)
        nonstatic = other->nonstatic;

}
OverloadSet::OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangUtil::ClangTU* tu, Type* context)
    : clangfuncs(std::move(clangdecls)), from(tu), nonstatic(nullptr)
{
    if(dynamic_cast<ClangType*>(context->Decay()))
        nonstatic = context;
}
