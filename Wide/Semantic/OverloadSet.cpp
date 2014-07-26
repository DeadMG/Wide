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
#include <Wide/Util/DebugUtilities.h>
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

std::shared_ptr<Expression> OverloadSet::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    std::vector<Type*> targs;

    if (nonstatic)
        targs.push_back(nonstatic);

    for(auto&& x : args)
        targs.push_back(x->GetType());
    auto call = Resolve(targs, c.from);
    if (!call) IssueResolutionError(targs, c);

    if (nonstatic)
        args.insert(args.begin(), CreatePrimUnOp(BuildValue(std::move(val)), nonstatic, [](llvm::Value* self, CodegenContext& con) {
            return con->CreateExtractValue(self, { 0 });
        }));

    if (val)
        return BuildChain(std::move(val), call->Call(std::move(args), c));
    return call->Call(std::move(args), c);
}

OverloadSet::OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* t, Analyzer& a)
: callables(std::move(call)), from(nullptr), nonstatic(t), AggregateType(a) {}

struct cppcallable : public Callable {
    Type* source;
    clang::FunctionDecl* fun;
    ClangTU* from;
    std::vector<std::pair<Type*, bool>> types;

    std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        struct CPPSelf : Expression {
            CPPSelf(clang::FunctionDecl* func, ClangTU* from, Type* fty, std::shared_ptr<Expression> arg)
            : func(func), from(from), fty(fty), self(arg) 
            {
                if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(func))
                    fun = from->GetObject(con, clang::CXXCtorType::Ctor_Complete);
                else if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(func))
                    fun = from->GetObject(des, clang::CXXDtorType::Dtor_Complete);
                else
                    fun = from->GetObject(func);
                if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(func)) {
                    if (meth->isVirtual()) {
                        auto selfty = self->GetType();
                        if (selfty->IsReference()) {
                            auto clangty = dynamic_cast<ClangType*>(selfty->Decay());
                            vtable = clangty->GetVirtualPointer(self);
                        }
                    }
                }
            }
            clang::FunctionDecl* func;
            ClangTU* from;
            Type* fty;
            std::shared_ptr<Expression> self;
            std::shared_ptr<Expression> vtable;
            std::function<llvm::Function*(llvm::Module*)> fun;

            Type* GetType() override final {
                return fty;
            }
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                if (vtable)
                    return con->CreateBitCast(con->CreateLoad(con->CreateConstGEP1_32(con->CreateLoad(vtable->GetValue(con)), from->GetVirtualFunctionOffset(llvm::dyn_cast<clang::CXXMethodDecl>(func), con))), fty->GetLLVMType(con));
                auto llvmfunc = fun(con);
                // Clang often generates functions with the wrong signature.
                // But supplies attributes for a function with the right signature.
                // This is super bad when the right signature has more arguments, as the verifier rejects the declaration.                
                if (llvmfunc->getType() != fty->GetLLVMType(con)) {
                    if (std::distance(llvmfunc->use_begin(), llvmfunc->use_end()) == 0 && llvmfunc->getBasicBlockList().empty() && llvmfunc->getLinkage() == llvm::GlobalValue::ExternalLinkage) {
                        // Clang has no uses of this decl. Just erase the declaration and create our own.
                        auto mangledname = llvmfunc->getName();
                        auto cconv = llvmfunc->getCallingConv();
                        llvmfunc->removeFromParent();
                        auto functy = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(fty->GetLLVMType(con))->getElementType());
                        llvmfunc = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, mangledname, con.module);
                        llvmfunc->setCallingConv(cconv);
                        return llvmfunc;
                    }
                    return con->CreateBitCast(llvmfunc, fty->GetLLVMType(con));
                }
                return llvmfunc;
            }
        };
        std::vector<Type*> local;
        for (auto x : types)
            local.push_back(x.first);
        auto&& analyzer = source->analyzer;
        auto fty = analyzer.GetFunctionType(analyzer.GetClangType(*from, fun->getResultType()), local, fun->isVariadic(), GetCallingConvention(fun));
        auto self = args.size() > 0 ? args[0] : nullptr;
        return fty->BuildCall(Wide::Memory::MakeUnique<CPPSelf>(fun, from, fty, self), std::move(args), { source, c.where });
    }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
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
            if (types[i].first == args[i]->GetType()) {
                out.push_back(std::move(args[i]));
                continue;
            }

            // Handle base type mismatches first because else they are mishandled by the next check.
            auto derived = args[i]->GetType()->Decay(); 
            auto base = types[i].first->Decay();
            if (derived->IsDerivedFrom(types[i].first->Decay()) == Type::InheritanceRelationship::UnambiguouslyDerived) {
                out.push_back(types[i].first->BuildValueConstruction({ std::move(args[i]) }, c));
                continue;
            }

            // Clang may ask us to build an int8* from a string literal.
            // Just pretend that the argument was an int8*.
            struct ImplicitStringDecay : Expression {
                ImplicitStringDecay(std::shared_ptr<Expression> expr)
                : StringExpr(std::move(expr)) {}
                std::shared_ptr<Expression> StringExpr;
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    return StringExpr->GetValue(con);
                }
                Type* GetType() override final {
                    auto&& analyzer = StringExpr->GetType()->analyzer;
                    return analyzer.GetPointerType(analyzer.GetIntegralType(8, true));
                }
            };

            // If the function takes a const lvalue (as kindly provided by the second member),
            // and we provided an rvalue, pretend secretly that it took an rvalue reference instead.
            // Since is-a does not apply here, build construction from the underlying type, rather than going through rvaluetype.

            // This section handles stuff like const i32& i = int64() and similar implicit conversions that Wide accepts explicitly.
            // where C++ accepts the result by rvalue reference or const reference.
            // As per usual, careful of infinite recursion. Took quite a few tries to get this piece apparently functional.
            if (
                (types[i].second || IsRvalueType(types[i].first)) && 
                !IsLvalueType(args[i]->GetType())
            ) {
                if (types[i].first->Decay() == args[i]->GetType()->Decay())
                    out.push_back(types[i].first->analyzer.GetRvalueType(types[i].first->Decay())->BuildValueConstruction({ std::move(args[i]) }, c));
                else {
                    if (types[i].first->Decay() == source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)) && dynamic_cast<StringType*>(args[i]->GetType()->Decay()))
                        out.push_back(types[i].first->Decay()->BuildRvalueConstruction({ Wide::Memory::MakeUnique<ImplicitStringDecay>(BuildValue(std::move(args[i]))) }, c));
                    else
                        out.push_back(types[i].first->Decay()->BuildRvalueConstruction({ std::move(args[i]) }, c));
                }
                continue;
            }
            
            // Clang may ask us to build an int8* from a string literal.
            // Just pretend that the argument was an int8*.
            if (types[i].first->Decay() == source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)) && dynamic_cast<StringType*>(args[i]->GetType()->Decay())) {
                out.push_back(types[i].first->BuildValueConstruction({ Wide::Memory::MakeUnique<ImplicitStringDecay>(BuildValue(std::move(args[i]))) }, c));
                continue;
            }
            
            out.push_back(types[i].first->BuildValueConstruction({ std::move(args[i]) }, c));
        }
        return out;
    }
};

Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Type* source) {
    // Terrible hack but need to get this to work right now

    std::vector<std::pair<OverloadResolvable*, std::vector<Type*>>> call;
    for(auto funcobj : callables) {
        auto matched_types = funcobj->MatchParameter(f_args, analyzer, source);
        if (matched_types) {
            auto&& vec = *matched_types;
            assert(vec.size() == f_args.size());
            call.push_back(std::make_pair(funcobj, std::move(*matched_types)));
        }
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
    if (call.size() > 1) {
        std::sort(call.begin(), call.end(), is_more_specialized);
        if (is_more_specialized(call[0], call[1]))
            call.erase(call.begin() + 1, call.end());
        // Fuck. Maybe Clang will produce a result?
    }
    

    auto get_wide_or_result = [&]() -> Callable* {
        if (call.size() == 1)
            return call.begin()->first->GetCallableForResolution(call.begin()->second, analyzer);
        if (call.size() > 1)
            Wide::Util::DebugBreak();
        return nullptr;
    };

    std::vector<clang::QualType> clangtypes;
    if (from) {
        for (auto arg : f_args) {
            auto ty = arg->GetClangType(*from);
            if (!ty) return get_wide_or_result();
            clangtypes.push_back(*ty);
        }
        std::list<clang::OpaqueValueExpr> exprs;
        for(auto x : f_args)
            exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x->GetClangType(*from)->getNonLValueExprType(from->GetASTContext()), GetKindOfType(x)));
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
        if (wide_result)
            throw std::runtime_error("Attempted to resolve an overload set, but both Wide and C++ provided viable results.");
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
        return p;
    }
    return get_wide_or_result();
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
        if (dynamic_cast<MemberFunctionContext*>(context->Decay()))
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
        ReferenceConstructor = MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                auto agg = llvm::ConstantAggregateZero::get(GetLLVMType(con));
                return con->CreateInsertValue(agg, rhs, { 0 });
            });
        }, { analyzer.GetLvalueType(this), nonstatic });
        constructors.insert(ReferenceConstructor.get());

        return analyzer.GetOverloadSet(AggregateType::CreateConstructorOverloadSet({ false, true, true }), analyzer.GetOverloadSet(constructors));
    }
        
    return AggregateType::CreateConstructorOverloadSet(access);
}
std::shared_ptr<Expression> OverloadSet::AccessMember(std::shared_ptr<Expression> t, std::string name, Context c) {
    if (name != "resolve")
        return Type::AccessMember(std::move(t), name, c);
    if (ResolveType)
        return ResolveType->BuildValueConstruction({}, Context( this, c.where ));
    struct ResolveCallable : public MetaType 
    {
        ResolveCallable(OverloadSet* f, Analyzer& a)
        : from(f), MetaType(a) {}
        OverloadSet* from;
        std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            std::vector<Type*> types;
            for (auto&& arg : args) {
                auto con = dynamic_cast<ConstructorType*>(arg->GetType()->Decay());
                if (!con) throw std::runtime_error("Attempted to resolve but an argument was not a type.");
                types.push_back(con->GetConstructedType());
            }
            auto call = from->Resolve(types, c.from);
            if (!call) from->IssueResolutionError(types, c);
            auto clangfunc = dynamic_cast<cppcallable*>(call);
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(clangfunc->fun);
            return analyzer.GetOverloadSet(decls, clangfunc->from, nullptr)->BuildValueConstruction({}, c);
        }
        std::string explain() override final {
            return from->explain() + ".resolve";
        }
    };
    if (!ResolveType) ResolveType = Wide::Memory::MakeUnique<ResolveCallable>(this, analyzer);
    return ResolveType->BuildValueConstruction({}, Context( this, c.where ));
}
std::string OverloadSet::explain() {
    std::stringstream strstr;
    strstr << this;
    return "OverloadSet " + strstr.str();
}

void OverloadSet::IssueResolutionError(std::vector<Type*> types, Context c) {
    throw OverloadResolutionFailure(c.where);
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