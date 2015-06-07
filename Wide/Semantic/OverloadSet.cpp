#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Expression.h>
#include <array>
#include <sstream>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/Overload.h>
#include <clang/Sema/Template.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

template<typename T, typename U> T* debug_cast(U* other) {
    assert(dynamic_cast<T*>(other));
    return static_cast<T*>(other);
}

std::shared_ptr<Expression> OverloadSet::ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    auto argscopy = args;
    std::vector<Type*> targs;

    if (nonstatic)
        targs.push_back(nonstatic);
    for (auto&& x : argscopy)
        targs.push_back(x->GetType(key));
    auto call = Resolve(targs, c.from);
    if (!call) return IssueResolutionError(targs, c);

    if (nonstatic)
        argscopy.insert(argscopy.begin(), CreatePrimUnOp(BuildValue(std::move(val)), nonstatic, [](llvm::Value* self, CodegenContext& con) {
            return con->CreateExtractValue(self, { 0 });
        }));

    if (val)
        return BuildChain(std::move(val), call->Call(key, std::move(argscopy), c));
    return call->Call(key, std::move(argscopy), c);
}

OverloadSet::OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* t, Analyzer& a)
: callables(std::move(call)), from(nullptr), nonstatic(t), AggregateType(a) {}

struct cppcallable : public Callable {
    Type* source;
    clang::FunctionDecl* fun;
    ClangTU* from;
    std::vector<std::pair<Type*, bool>> types;

    std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        std::vector<Type*> local;
        for (auto x : types)
            local.push_back(x.first);
        auto&& analyzer = source->analyzer;
        auto fty = GetFunctionType(fun, *from, analyzer);
        auto self = args.size() > 0 ? args[0] : nullptr;
        std::function<llvm::Function*(llvm::Module*)> object;
        if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(fun))
            object = from->GetObject(fty->analyzer, con, clang::CXXCtorType::Ctor_Complete);
        else if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(fun))
            object = from->GetObject(fty->analyzer, des, clang::CXXDtorType::Dtor_Complete);
        else
            object = from->GetObject(fty->analyzer, fun);
        std::shared_ptr<Expression> vtable;
        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(fun)) {
            if (meth->isVirtual()) {
                auto selfty = self->GetType(key);
                if (selfty->IsReference()) {
                    auto clangty = dynamic_cast<ClangType*>(selfty->Decay());
                    vtable = Type::GetVirtualPointer(key, self);
                }
            }
        }
        auto func = CreatePrimGlobal(Range::Empty(), fty, [=](CodegenContext& con)-> llvm::Value* {
            if (vtable)
                return con->CreateBitCast(con->CreateLoad(con->CreateConstGEP1_32(con->CreateLoad(vtable->GetValue(con)), from->GetVirtualFunctionOffset(llvm::dyn_cast<clang::CXXMethodDecl>(fun), con))), fty->GetLLVMType(con));
            auto llvmfunc = object(con);
            // Clang often generates functions with the wrong signature.
            // But supplies attributes for a function with the right signature.
            // This is super bad when the right signature has more arguments, as the verifier rejects the declaration.                
            if (llvmfunc->getType() != fty->GetLLVMType(con))
                return con->CreateBitCast(llvmfunc, fty->GetLLVMType(con));
            return llvmfunc;
        });
        return Type::BuildCall(key, func, args, c);
    }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        // Clang may resolve a static function. Drop "this" if it did.
        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(fun))
            if (meth->isStatic())
                args.erase(args.begin());
        // Clang may ask us to call an overload that doesn't have the right number of arguments
        // because it has a default argument.
        // types includes this when Clang doesn't include it, so subtract 1 for that.
        auto baseargsize = args.size();
        if (baseargsize != types.size()) {
            for (std::size_t i = 0; i < (types.size() - baseargsize); ++i) {
                auto argnum = i + baseargsize;
                if (fun->isCXXInstanceMember())
                    argnum--;
                auto paramdecl = fun->getParamDecl(argnum);
                if (paramdecl->hasUninstantiatedDefaultArg()) {
                    auto list = fun->getTemplateSpecializationArgs();
                    from->GetSema().SetParamDefaultArgument(paramdecl, from->GetSema().SubstInitializer(paramdecl->getUninstantiatedDefaultArg(), from->GetSema().getTemplateInstantiationArgs(fun), paramdecl->isDirectInit()).get(), clang::SourceLocation());
                }
                args.push_back(InterpretExpression(paramdecl->getDefaultArg(), *from, { source, c.where }, source->analyzer));
            }
        }
        // Clang may ask us to call overloads where we think the arguments are not a match
        // for example, implicit conversion from int64 to int32
        // so do the conversion ourselves if lvalues were not involved.
        std::vector<std::shared_ptr<Expression>> out;
        for (std::size_t i = 0; i < types.size(); ++i) {
            auto get_expr = [=] {
                if (types[i].first == args[i]->GetType(key))
                    return std::move(args[i]);

                // Handle base type mismatches first because else they are mishandled by the next check.
                auto derived = args[i]->GetType(key)->Decay();
                auto base = types[i].first->Decay();
                if (derived->IsDerivedFrom(types[i].first->Decay()) == Type::InheritanceRelationship::UnambiguouslyDerived)
                    return types[i].first->BuildValueConstruction(key, { std::move(args[i]) }, c);

                // Clang may ask us to build an int8* from a string literal.
                // Just pretend that the argument was an int8*.

                auto ImplicitStringDecay = [this](std::shared_ptr<Expression> expr) {
                    return CreatePrimGlobal(Range::Elements(expr), source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)), [=](CodegenContext& con) {
                        return expr->GetValue(con);
                    });
                };

                // If the function takes a const lvalue (as kindly provided by the second member),
                // and we provided an rvalue, pretend secretly that it took an rvalue reference instead.
                // Since is-a does not apply here, build construction from the underlying type, rather than going through rvaluetype.

                // This section handles stuff like const i32& i = int64() and similar implicit conversions that Wide accepts explicitly.
                // where C++ accepts the result by rvalue reference or const reference.
                // As per usual, careful of infinite recursion. Took quite a few tries to get this piece apparently functional.
                if ((types[i].second || IsRvalueType(types[i].first)) && !IsLvalueType(args[i]->GetType(key))) {
                    if (types[i].first->Decay() == args[i]->GetType(key)->Decay())
                        return types[i].first->analyzer.GetRvalueType(types[i].first->Decay())->BuildValueConstruction(key, { std::move(args[i]) }, c);
                    else {
                        if (types[i].first->Decay() == source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)) && dynamic_cast<StringType*>(args[i]->GetType(key)->Decay()))
                            return types[i].first->Decay()->BuildRvalueConstruction(key, { ImplicitStringDecay(BuildValue(std::move(args[i]))) }, c);
                        else
                            return types[i].first->Decay()->BuildRvalueConstruction(key, { std::move(args[i]) }, c);
                    }
                }

                // Clang may ask us to build an int8* from a string literal.
                // Just pretend that the argument was an int8*.
                if (types[i].first->Decay() == source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)) && dynamic_cast<StringType*>(args[i]->GetType(key)->Decay()))
                    return types[i].first->BuildValueConstruction(key, { ImplicitStringDecay(BuildValue(std::move(args[i]))) }, c);

                return types[i].first->BuildValueConstruction(key, { std::move(args[i]) }, c);
            };
            out.push_back(get_expr());
        }
        return out;
    }
};

std::pair<Callable*, std::vector<Type*>> OverloadSet::ResolveWithArguments(std::vector<Type*> f_args, Type* source) {
    // Terrible hack but need to get this to work right now
    std::vector<std::pair<OverloadResolvable*, std::vector<Type*>>> call;
    for (auto funcobj : callables) {
        auto matched_types = funcobj->MatchParameter(f_args, analyzer, source);
        if (matched_types) {
            auto&& vec = *matched_types;
            assert(vec.size() == f_args.size());
            call.push_back(std::make_pair(funcobj, std::move(*matched_types)));
        }
    }
    // Try an exact match specialization.
    if (call.size() > 1) {
        auto is_more_specialized = [&](
            const std::pair<OverloadResolvable*, std::vector<Type*>>& lhs,
            const std::pair<OverloadResolvable*, std::vector<Type*>>& rhs
            ) {
            // If, for some argument, it is an EXACT match, and
            // the equivalent argument is not an EXACT match, and
            // there is no other argument for which the reverse is true
            // it is more specialized.
            bool is_more_specialized = false;
            bool is_less_specialized = false;
            for (std::size_t i = 0; i < f_args.size(); ++i) {
                is_more_specialized = is_more_specialized || (lhs.second[i] == f_args[i] && rhs.second[i] != f_args[i]);
                is_less_specialized = is_less_specialized || (rhs.second[i] == f_args[i] && lhs.second[i] != f_args[i]);
            }
            return is_more_specialized && !is_less_specialized;
        };

        std::sort(call.begin(), call.end(), is_more_specialized);
        if (is_more_specialized(call[0], call[1]))
            call.erase(call.begin() + 1, call.end());
    }
    // Try is-a match now.
    if (call.size() > 1) {
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
                if (Type::IsFirstASecond(lhs, rhs, source))
                    if (!Type::IsFirstASecond(rhs, lhs, source))
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
        std::sort(call.begin(), call.end(), is_more_specialized);
        if (is_more_specialized(call[0], call[1]))
            call.erase(call.begin() + 1, call.end());
    }

    auto get_wide_or_result = [&]() -> std::pair<Callable*, std::vector<Type*>> {
        if (call.size() == 1)
            return std::make_pair(call.begin()->first->GetCallableForResolution(call.begin()->second, source, analyzer), call.begin()->second);
        assert(call.size() == 0);
        return std::make_pair(nullptr, std::vector<Type*>());
    };

    std::vector<clang::QualType> clangtypes;
    if (from) {
        for (auto arg : f_args) {
            auto ty = arg->GetClangType(*from);
            if (!ty) return get_wide_or_result();
            clangtypes.push_back(*ty);
        }
        std::list<clang::OpaqueValueExpr> exprs;
        for (auto x : f_args)
            exprs.push_back(clang::OpaqueValueExpr(from->GetFileEnd(), x->GetClangType(*from)->getNonLValueExprType(from->GetASTContext()), GetKindOfType(x)));
        std::vector<clang::Expr*> exprptrs;
        for (auto&& x : exprs)
            exprptrs.push_back(&x);
        clang::OverloadCandidateSet s(clang::SourceLocation(), clang::OverloadCandidateSet::CandidateSetKind::CSK_Normal);
        clang::UnresolvedSet<8> us;
        bool has_members = false;
        for (auto decl : clangfuncs) {
            if (llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                has_members = true;
                continue;
            }
            if (auto funtempl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl)) {
                if (llvm::dyn_cast<clang::CXXMethodDecl>(funtempl->getTemplatedDecl())) {
                    has_members = true;
                    continue;
                }
            }
            us.addDecl(decl);
        }
        from->GetSema().AddFunctionCandidates(us, exprptrs, s, false, nullptr);
        if (has_members && !exprptrs.empty()) {
            exprptrs.erase(exprptrs.begin());
            for (auto decl : clangfuncs) {
                auto funtempl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl);
                if (llvm::dyn_cast<clang::CXXConstructorDecl>(decl) || funtempl && llvm::dyn_cast<clang::CXXConstructorDecl>(funtempl->getTemplatedDecl())) {
                    clang::DeclAccessPair d;
                    d.setDecl(decl);
                    d.setAccess(decl->getAccess());
                    if (funtempl)
                        from->GetSema().AddTemplateOverloadCandidate(funtempl, d, nullptr, exprptrs, s, false);
                    else
                        from->GetSema().AddOverloadCandidate(llvm::cast<clang::FunctionDecl>(decl), d, exprptrs, s, false, false, true);
                    continue;
                }
                if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl) || funtempl && llvm::dyn_cast<clang::CXXMethodDecl>(funtempl->getTemplatedDecl())) {
                    clang::DeclAccessPair d;
                    d.setDecl(decl);
                    d.setAccess(decl->getAccess());
                    auto ty = f_args[0]->GetClangType(*from)->getNonLValueExprType(from->GetASTContext());
                    clang::OpaqueValueExpr valuexpr(clang::SourceLocation(), ty, Wide::Semantic::GetKindOfType(f_args[0]));
                    from->GetSema().AddMethodCandidate(d, ty, valuexpr.Classify(from->GetASTContext()), exprptrs, s, false);
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
        if (wide_result.first)
            return std::make_pair(nullptr, std::vector<Type*>());
        auto fun = best->Function;
        auto p = new cppcallable();
        p->fun = fun;
        p->from = from;
        p->source = source;
        // The function may be expecting a base type.
        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(p->fun)) {
            if (!meth->isStatic()) {
                if (IsLvalueType(f_args[0]))
                    p->types.push_back(std::make_pair(analyzer.GetLvalueType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(meth->getParent()))), false));
                else
                    p->types.push_back(std::make_pair(analyzer.GetRvalueType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(meth->getParent()))), false));
            }
        }
        for (unsigned i = 0; i < fun->getNumParams(); ++i) {
            auto argty = fun->getParamDecl(i)->getType();
            auto arg_is_const_ref = argty->isReferenceType() && (argty->getPointeeType().isConstQualified() || argty->getPointeeType().isLocalConstQualified());
            p->types.push_back(std::make_pair(analyzer.GetClangType(*from, argty), arg_is_const_ref));
        }
        std::vector<Type*> types;
        for (auto pair : p->types)
            types.push_back(pair.first);
        return std::make_pair(p, types);
    }
    return get_wide_or_result();
}
Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Type* source) {
    return ResolveWithArguments(f_args, source).first;
}

OverloadSet::OverloadSet(OverloadSet* s, OverloadSet* other, Analyzer& a, Type* context)
: AggregateType(a), nonstatic(nullptr) {
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
    if (!context) {
        if (s->nonstatic && other->nonstatic) {
            assert(s->nonstatic != other->nonstatic && "Attempted to combine an overload set of two overload sets containing functions which are members of two different types.");
            nonstatic = s->nonstatic;
        }
    } else {
        nonstatic = context;
    }
}
OverloadSet::OverloadSet(std::unordered_set<clang::NamedDecl*> clangdecls, ClangTU* tu, Type* context, Analyzer& a)
: clangfuncs(std::move(clangdecls)), from(tu), AggregateType(a), nonstatic(nullptr)
{
    if(context)
        if(dynamic_cast<ClangType*>(context->Decay()))
            nonstatic = context;
}
OverloadSet* OverloadSet::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);
    if (nonstatic) {
        std::unordered_set<OverloadResolvable*> constructors;
        ReferenceConstructor = MakeResolvable([this](Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(key, std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                auto agg = llvm::ConstantAggregateZero::get(GetLLVMType(con));
                return con->CreateInsertValue(agg, rhs, { 0 });
            });
        }, { analyzer.GetLvalueType(this), nonstatic });
        constructors.insert(ReferenceConstructor.get());

        return analyzer.GetOverloadSet(AggregateType::CreateConstructorOverloadSet({ false, true, true }), analyzer.GetOverloadSet(constructors));
    }
        
    return AggregateType::CreateConstructorOverloadSet(access);
}
std::shared_ptr<Expression> OverloadSet::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) {
    if (name != "resolve")
        return Type::AccessMember(key, std::move(t), name, c);
    if (ResolveResolvable)
        return analyzer.GetOverloadSet(ResolveResolvable.get())->BuildValueConstruction(key, {}, c);
    struct ResolveCallable : public OverloadResolvable, public Callable
    {
        ResolveCallable(OverloadSet* f, Analyzer& a)
        : from(f) {}
        OverloadSet* from;
        Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            for (auto ty : types)
                if (!dynamic_cast<ConstructorType*>(ty))
                    return Wide::Util::none;
            return types;
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final {
            return this;
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return args;
        }
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            std::vector<Type*> types;
            for (auto&& arg : args) {
                auto con = dynamic_cast<ConstructorType*>(arg->GetType(key)->Decay());
                assert(con && "Called CallFunction with conditions OR should have prevented.");
                types.push_back(con->GetConstructedType());
            }
            auto call = from->Resolve(types, c.from);
            if (!call) return from->IssueResolutionError(types, c);
            auto clangfunc = dynamic_cast<cppcallable*>(call);
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(clangfunc->fun);
            return from->analyzer.GetOverloadSet(decls, clangfunc->from, nullptr)->BuildValueConstruction(key, {}, c);
        }
    };
    if (!ResolveResolvable) ResolveResolvable = Wide::Memory::MakeUnique<ResolveCallable>(this, analyzer);
    return analyzer.GetOverloadSet(ResolveResolvable.get())->BuildValueConstruction(key, {}, c);
}
std::string OverloadSet::explain() {
    std::stringstream strstr;
    strstr << this;
    return "OverloadSet " + strstr.str();
}

std::shared_ptr<Expression> OverloadSet::IssueResolutionError(std::vector<Type*> types, Context c) {
    return CreateErrorExpression(Memory::MakeUnique<SpecificError<OverloadResolutionFailed>>(analyzer, c.where, "Overload resolution failed."));
}
std::pair<ClangTU*, clang::FunctionDecl*> OverloadSet::GetSingleFunction() {
    if (clangfuncs.size() == 1) {
        if (auto funcdecl = llvm::dyn_cast<clang::FunctionDecl>(*clangfuncs.begin())) {
            return { from, funcdecl };
        }
    }
    return { nullptr, nullptr };
}

std::vector<Type*> OverloadSet::GetMembers() {
    if (nonstatic && contents.empty()) {
        contents.push_back(nonstatic);
    }
    return contents;
}

bool OverloadSet::IsConstant() {
    return !IsNonstatic();
}