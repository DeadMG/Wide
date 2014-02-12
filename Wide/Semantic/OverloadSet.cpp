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

ConcreteExpression OverloadSet::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
    std::vector<Type*> targs;

    if (nonstatic)
        targs.push_back(nonstatic);

    for(auto x : args)
        targs.push_back(x.t);
    auto call = Resolve(std::move(targs), *c);
    if (!call)
        throw std::runtime_error("Fuck!");

    if (nonstatic)
        args.insert(args.begin(), ConcreteExpression(nonstatic, c->gen->CreateFieldExpression(e.BuildValue(c).Expr, 0)));

    return call->Call(std::move(args), c);
}

OverloadSet::OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* t)
: callables(std::move(call)), from(nullptr), nonstatic(t), ResolveOverloadSet(nullptr) {}

struct cppcallable : public Callable {
    clang::FunctionDecl* fun;
    ClangUtil::ClangTU* from;
    std::vector<Type*> types;

    ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
        return ConcreteExpression(c->GetFunctionType(c->GetClangType(*from, fun->getResultType()), types), c->gen->CreateFunctionValue(from->MangleName(fun))).BuildCall(args, c);
    }
    std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final {
        // Clang may ask us to call overloads where we think the arguments are not a match
        // for example, implicit conversion from int64 to int32
        if (args.size() != types.size())
            Wide::Util::DebugBreak();
        std::vector<ConcreteExpression> out;
        for (std::size_t i = 0; i < types.size(); ++i) {
            out.push_back(types[i]->BuildValueConstruction(args[i], c));
        }
        return out;
    }
};

Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Analyzer& a) {
    std::vector<std::pair<OverloadResolvable*, std::vector<Type*>>> call;
    for(auto funcobj : callables) {
        std::vector<Type*> matched_types;
        if (funcobj->GetArgumentCount() != f_args.size())
            continue;
        bool fail = false;
        for(std::size_t i = 0; i < f_args.size(); ++i) {
            auto argty = f_args[i];
            if (!argty->IsReference())
                argty = a.GetRvalueType(f_args[i]);
            auto paramty = funcobj->MatchParameter(f_args[i], i, a);
            if (!paramty) {
                fail = true;
                break;
            }
            matched_types.push_back(paramty);
        }
        if (!fail)
            call.push_back(std::make_pair(funcobj, std::move(matched_types)));
    }
    // returns true if lhs is more specialized than rhs
    auto is_more_specialized = [&](
        const std::pair<OverloadResolvable*, std::vector<Type*>>& lhs,
        const std::pair<OverloadResolvable*, std::vector<Type*>>& rhs
    ) {
        // If, for some argument, it is the equivalent argument, and
        // the equivalent argument is not that argument, and
        // there is no other argument for which the reverse is true
        // it is more specialized.
        auto is_argument_more_specialized = [&](Type* lhs, Type* rhs) {
            if (lhs->IsA(lhs, rhs, a))
                if (!rhs->IsA(rhs, lhs, a))
                    return true;
            return false;
        };
        bool is_more_specialized = false;
        bool is_less_specialized = false;
        for (std::size_t i = 0; i < f_args.size(); ++i) {
            is_more_specialized = is_more_specialized || is_argument_more_specialized(lhs.second[i], rhs.second[i]);
            is_less_specialized = is_less_specialized || is_argument_more_specialized(rhs.second[i], lhs.second[i]);
        }
        return is_more_specialized && !is_less_specialized;
    };
    if (call.size() > 1) {
        std::sort(call.begin(), call.end(), is_more_specialized);
        if (is_more_specialized(call[0], call[1]))
            call.erase(call.begin() + 1, call.end());
        // Fuck. Maybe Clang will produce a result?
    }
    

    auto get_wide_or_result = [&]() -> Callable* {
        if (call.size() == 1)
            return call.begin()->first->GetCallableForResolution(call.begin()->second, a);
        if (call.size() > 1)
            Wide::Util::DebugBreak();
        return nullptr;
    };

    if (from) {    
        std::list<clang::OpaqueValueExpr> exprs;
        for(auto x : f_args)
            exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x)));
        std::vector<clang::Expr*> exprptrs;
        for(auto&& x : exprs)
            exprptrs.push_back(&x);
        clang::OverloadCandidateSet s((clang::SourceLocation()));
        clang::UnresolvedSet<8> us;
        bool has_members = false;
        for (auto decl : clangfuncs) {
            if (llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                has_members = true;
                continue;
            }
            us.addDecl(decl);
        }
        from->GetSema().AddFunctionCandidates(us, exprptrs, s, false, nullptr);
        if (has_members && !exprptrs.empty()) {
            exprptrs.erase(exprptrs.begin());
            for (auto decl : clangfuncs) {
                if (llvm::dyn_cast<clang::CXXConstructorDecl>(decl)) {
                    clang::DeclAccessPair d;
                    d.setDecl(decl);
                    d.setAccess(decl->getAccess());
                    from->GetSema().AddOverloadCandidate(llvm::cast<clang::FunctionDecl>(decl), d, exprptrs, s, false, false, true);
                    continue;
                }
                if (llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                    clang::DeclAccessPair d;
                    d.setDecl(decl);
                    d.setAccess(decl->getAccess());
                    from->GetSema().AddMethodCandidate(d, f_args[0]->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), clang::Expr::Classification::makeSimpleLValue(), exprptrs, s, false);
                    continue;
                }
            }
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
        auto p = new cppcallable();
        p->fun = fun;
        p->from = from;
        if (llvm::dyn_cast<clang::CXXMethodDecl>(p->fun))
            p->types.push_back(f_args[0]);
        for (unsigned i = 0; i < fun->getNumParams(); ++i) {
            // If the function takes a const T&, and we provided an rvalue, pretend secretly that it took an rvalue reference instead.
            auto argty = fun->getParamDecl(i)->getType();            
            if (argty->isReferenceType() && (argty->getPointeeType().isConstQualified() || argty->getPointeeType().isLocalConstQualified()) && (f_args[p->types.size()] == a.GetRvalueType(f_args[p->types.size()]->Decay()) || f_args[p->types.size()] == f_args[p->types.size()]->Decay())) {
                p->types.push_back(a.GetClangType(*from, from->GetASTContext().getRValueReferenceType(argty->getLocallyUnqualifiedSingleStepDesugaredType())));
            } else
                p->types.push_back(a.GetClangType(*from, argty));
        }
        return p;
    }
    return get_wide_or_result();
}

std::size_t OverloadSet::size(Analyzer& a) {
    if (!nonstatic) return a.gen->GetInt8AllocSize();
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t OverloadSet::alignment(Analyzer& a) {
    if (!nonstatic) return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8);
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}
OverloadSet::OverloadSet(OverloadSet* s, OverloadSet* other)
: nonstatic(nullptr), ResolveOverloadSet(nullptr) {
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
    if (s->nonstatic && other->nonstatic) {
        assert(s->nonstatic != other->nonstatic && "Attempted to combine an overload set of two overload sets containing functions which are members of two different types.");
        nonstatic = s->nonstatic;
    }
}
OverloadSet::OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangUtil::ClangTU* tu, Type* context)
: clangfuncs(std::move(clangdecls)), from(tu), nonstatic(nullptr), ResolveOverloadSet(nullptr)
{
    if(context)
        if(dynamic_cast<ClangType*>(context->Decay()))
            nonstatic = context;
}

Type* OverloadSet::GetConstantContext(Analyzer& a) {
    if (!nonstatic) return this;
    return nullptr;
}
OverloadSet* OverloadSet::CreateConstructorOverloadSet(Analyzer& a) {
    std::unordered_set<OverloadResolvable*> constructors;
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    if (nonstatic) {
        types.push_back(this);
        constructors.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) { return ConcreteExpression(args[0].t, c->gen->CreateStore(c->gen->CreateFieldExpression(args[0].Expr, 0), c->gen->CreateFieldExpression(args[1].Expr, 0))); }, types, a));
        types.pop_back();
        assert(nonstatic->IsReference());
        types.push_back(nonstatic);
        constructors.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) { return ConcreteExpression(args[0].t, c->gen->CreateStore(c->gen->CreateFieldExpression(args[0].Expr, 0), args[1].Expr)); }, types, a));
        return a.GetOverloadSet(constructors);
    }
    constructors.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) { return args[0]; }, types, a));
    types.push_back(this);
    constructors.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) { return args[0]; }, types, a));
    return a.GetOverloadSet(constructors);
}
std::string OverloadSet::GetCPPMangledName() {
    if (clangfuncs.size() != 1)
        throw std::runtime_error("Attempted to get the mangled name of a Clang function, but it was an overload set.");
    auto decl = *clangfuncs.begin();
    return from->MangleName(decl);
}
Wide::Util::optional<ConcreteExpression> OverloadSet::AccessMember(ConcreteExpression self, std::string name, Context c) {
    if (name != "resolve")
        return Type::AccessMember(self, name, c);
    if (ResolveOverloadSet)
        return ResolveOverloadSet->BuildValueConstruction(c);
    struct ResolveCallable : public MetaType 
    {
        ResolveCallable(OverloadSet* f)
        : from(f) {}
        OverloadSet* from;
        ConcreteExpression BuildCall(ConcreteExpression self, std::vector<ConcreteExpression> args, Context c) override final {
            std::vector<Type*> types;
            for (auto arg : args) {
                auto con = dynamic_cast<ConstructorType*>(arg.t->Decay());
                if (!con) throw std::runtime_error("Attempted to resolve but an argument was not a type.");
                types.push_back(con->GetConstructedType());
            }
            auto call = from->Resolve(types, *c);
            if (!call)
                throw std::runtime_error("Could not resolve a single function from this overload set.");
            auto clangfunc = dynamic_cast<cppcallable*>(call);
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(clangfunc->fun);
            return c->GetOverloadSet(decls, clangfunc->from, nullptr)->BuildValueConstruction(c);
        }
    };
    ResolveOverloadSet = c->arena.Allocate<ResolveCallable>(this);
    return ResolveOverloadSet->BuildValueConstruction(c);
}