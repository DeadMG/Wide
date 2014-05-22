#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
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
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

template<typename T, typename U> T* debug_cast(U* other) {
    assert(dynamic_cast<T*>(other));
    return static_cast<T*>(other);
}

llvm::Type* OverloadSet::GetLLVMType(Codegen::Generator& g) {
    // Have to cache result - not fun.
    std::stringstream stream;
    stream << "struct.__" << this;
    auto str = stream.str();
    if (g.module->getTypeByName(str))
        return g.module->getTypeByName(str);
    if (!nonstatic) {
        auto int8ty = llvm::IntegerType::getInt8Ty(g.module->getContext());
        return llvm::StructType::create(str, int8ty, nullptr);        
    }
    auto ty = nonstatic->Decay()->GetLLVMType(g);
    return llvm::StructType::create(str, ty->getPointerTo(), nullptr);
}

std::unique_ptr<Expression> OverloadSet::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    std::vector<Type*> targs;

    if (nonstatic)
        targs.push_back(nonstatic);

    for(auto&& x : args)
        targs.push_back(x->GetType());
    auto call = Resolve(targs, c.from);
    if (!call) IssueResolutionError(targs);

    if (nonstatic)
        args.insert(args.begin(), CreatePrimUnOp(BuildValue(std::move(val)), nonstatic, [](llvm::Value* self, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
            return bb.CreateExtractValue(self, { 0 });
        }));

    return call->Call(std::move(args), c);
}

OverloadSet::OverloadSet(std::unordered_set<OverloadResolvable*> call, Type* t, Analyzer& a)
: callables(std::move(call)), from(nullptr), nonstatic(t), Type(a) {}

struct cppcallable : public Callable {
    Type* source;
    clang::FunctionDecl* fun;
    ClangTU* from;
    std::vector<std::pair<Type*, bool>> types;

    std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
        struct CPPSelf : Expression {
            CPPSelf(clang::FunctionDecl* func, ClangTU* from, Type* fty, Expression* arg)
            : func(func), from(from), fty(fty), self(arg) 
            {
                mangle = from->MangleName(func);
                if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(func)) {
                    if (meth->isVirtual()) {
                        auto selfty = self->GetType();
                        if (selfty->IsReference()) {
                            auto clangty = dynamic_cast<ClangType*>(selfty->Decay());
                            vtable = clangty->GetVirtualPointer(Wide::Memory::MakeUnique<ExpressionReference>(self));
                        }
                    }
                }
            }
            clang::FunctionDecl* func;
            ClangTU* from;
            Type* fty;
            Expression* self;
            std::unique_ptr<Expression> vtable;
            std::function<std::string(Codegen::Generator&)> mangle;

            Type* GetType() override final {
                return fty;
            }
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                if (vtable)
                    return bb.CreateBitCast(bb.CreateLoad(bb.CreateConstGEP1_32(bb.CreateLoad(vtable->GetValue(g, bb)), from->GetVirtualFunctionOffset(llvm::dyn_cast<clang::CXXMethodDecl>(func), g))), fty->GetLLVMType(g));
                auto llvmfunc = (llvm::Value*)g.module->getFunction(mangle(g));
                if (llvmfunc->getType() != fty->GetLLVMType(g))
                    llvmfunc = bb.CreateBitCast(llvmfunc, fty->GetLLVMType(g));
                return llvmfunc;
            }
            void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {}
        };
        std::vector<Type*> local;
        for (auto x : types)
            local.push_back(x.first);
        auto&& analyzer = source->analyzer;
        auto fty = analyzer.GetFunctionType(analyzer.GetClangType(*from, fun->getResultType()), local, fun->isVariadic());
        return fty->BuildCall(Wide::Memory::MakeUnique<CPPSelf>(fun, from, fty, args.size() > 0 ? args[0].get() : nullptr), std::move(args), { source, c.where });
    }
    std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
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
                if (!paramdecl->hasDefaultArg())
                    assert(false);
                args.push_back(InterpretExpression(paramdecl->getDefaultArg(), *from, { source, c.where }, source->analyzer));
            }
        }
        // Clang may ask us to call overloads where we think the arguments are not a match
        // for example, implicit conversion from int64 to int32
        // so do the conversion ourselves if lvalues were not involved.
        std::vector<std::unique_ptr<Expression>> out;
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (types[i].first == args[i]->GetType()) {
                out.push_back(std::move(args[i]));
                continue;
            }


            // Handle base type mismatches first because else they are mishandled by the next check.
            if (auto derived = dynamic_cast<BaseType*>(args[i]->GetType()->Decay())) {
                if (auto base = dynamic_cast<BaseType*>(types[i].first->Decay())) {
                    if (derived->IsDerivedFrom(types[i].first->Decay()) == InheritanceRelationship::UnambiguouslyDerived) {
                        out.push_back(types[i].first->BuildValueConstruction(Expressions(std::move(args[i])), c));
                        continue;
                    }
                }
            }
            
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
                    out.push_back(types[i].first->analyzer.GetRvalueType(types[i].first->Decay())->BuildValueConstruction(Expressions(std::move(args[i])), c));
                else
                    out.push_back(types[i].first->Decay()->BuildRvalueConstruction(Expressions(std::move(args[i])), c));
                continue;
            }

            struct ImplicitStringDecay : Expression {
                ImplicitStringDecay(std::unique_ptr<Expression> expr)
                : StringExpr(std::move(expr)) {}
                std::unique_ptr<Expression> StringExpr;
                void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                    StringExpr->DestroyLocals(g, bb);
                }
                llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                    return StringExpr->GetValue(g, bb);
                }
                Type* GetType() override final {
                    auto&& analyzer = StringExpr->GetType()->analyzer;
                    return analyzer.GetPointerType(analyzer.GetIntegralType(8, true));
                }
            };
            
            // Clang may ask us to build an int8* from a string literal.
            // Just pretend that the argument was an int8*.
            if (types[i].first->Decay() == source->analyzer.GetPointerType(source->analyzer.GetIntegralType(8, true)) && dynamic_cast<StringType*>(args[i]->GetType()->Decay())) {
                out.push_back(types[i].first->BuildValueConstruction(Expressions(Wide::Memory::MakeUnique<ImplicitStringDecay>(BuildValue(std::move(args[i])))), c));
                continue;
            }
            
            out.push_back(types[i].first->BuildValueConstruction(Expressions(std::move(args[i])), c));
        }
        return out;
    }
};

Callable* OverloadSet::Resolve(std::vector<Type*> f_args, Type* source) {
    // Terrible hack but need to get this to work right now

    std::vector<std::pair<OverloadResolvable*, std::vector<Type*>>> call;
    for(auto funcobj : callables) {
        auto matched_types = funcobj->MatchParameter(f_args, analyzer, source);
        if (matched_types) call.push_back(std::make_pair(funcobj, std::move(*matched_types)));
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
            if (lhs->IsA(lhs, rhs, GetAccessSpecifier(source, lhs)))
                if (!rhs->IsA(rhs, lhs, GetAccessSpecifier(source, rhs)))
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
                    from->GetSema().AddMethodCandidate(d, f_args[0]->GetClangType(*from)->getNonLValueExprType(from->GetASTContext()), clang::Expr::Classification::makeSimpleLValue(), exprptrs, s, false);
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
        p->source = source;
        // The function may be expecting a base type.
        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(p->fun)) {
            if (IsLvalueType(f_args[0]))
                p->types.push_back(std::make_pair(analyzer.GetLvalueType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(meth->getParent()))), false));
            else
                p->types.push_back(std::make_pair(analyzer.GetRvalueType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(meth->getParent()))), false));
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

std::size_t OverloadSet::size() {
    if (!nonstatic) return 1;
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t OverloadSet::alignment() {
    if (!nonstatic) return 1;
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
OverloadSet::OverloadSet(OverloadSet* s, OverloadSet* other, Analyzer& a, Type* context)
: Type(a), nonstatic(nullptr) {
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
: clangfuncs(std::move(clangdecls)), from(tu), Type(a), nonstatic(nullptr)
{
    if(context)
        if(dynamic_cast<ClangType*>(context->Decay()))
            nonstatic = context;
}

Type* OverloadSet::GetConstantContext() {
    if (!nonstatic) return this;
    return nullptr;
}
OverloadSet* OverloadSet::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    std::unordered_set<OverloadResolvable*> constructors;
    if (nonstatic) {
        CopyConstructor = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                return rhs;
            });
        }, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
        constructors.insert(CopyConstructor.get());

        MoveConstructor = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                return rhs;
            });
        }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
        constructors.insert(MoveConstructor.get());

        ReferenceConstructor = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                auto agg = llvm::ConstantAggregateZero::get(GetLLVMType(g));
                return bb.CreateInsertValue(agg, rhs, { 0 });
            });
        }, { analyzer.GetLvalueType(this), nonstatic });
        constructors.insert(ReferenceConstructor.get());

        return analyzer.GetOverloadSet(constructors);
    }

    DefaultConstructor = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context cm) {
        return std::move(args[0]);
    }, { analyzer.GetLvalueType(this) });
    constructors.insert(DefaultConstructor.get());

    CopyConstructor = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) {
        return std::move(args[0]);
    }, { analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) });
    constructors.insert(CopyConstructor.get());

    MoveConstructor = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) {
        return std::move(args[0]);
    }, { analyzer.GetLvalueType(this), analyzer.GetRvalueType(this) });
    constructors.insert(MoveConstructor.get());

    return analyzer.GetOverloadSet(constructors);
}
std::unique_ptr<Expression> OverloadSet::AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) {
    if (name != "resolve")
        return Type::AccessMember(std::move(t), name, c);
    if (ResolveType)
        return ResolveType->BuildValueConstruction({}, { this, c.where });
    struct ResolveCallable : public MetaType 
    {
        ResolveCallable(OverloadSet* f, Analyzer& a)
        : from(f), MetaType(a) {}
        OverloadSet* from;
        std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            std::vector<Type*> types;
            for (auto&& arg : args) {
                auto con = dynamic_cast<ConstructorType*>(arg->GetType()->Decay());
                if (!con) throw std::runtime_error("Attempted to resolve but an argument was not a type.");
                types.push_back(con->GetConstructedType());
            }
            auto call = from->Resolve(types, c.from);
            if (!call) from->IssueResolutionError(types);
            auto clangfunc = dynamic_cast<cppcallable*>(call);
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(clangfunc->fun);
            return analyzer.GetOverloadSet(decls, clangfunc->from, nullptr)->BuildValueConstruction(Expressions(), { from, c.where });
        }
        std::string explain() override final {
            return from->explain() + ".resolve";
        }
    };
    ResolveType = Wide::Memory::MakeUnique<ResolveCallable>(this, analyzer);
    return ResolveType->BuildValueConstruction(Expressions(), { this, c.where });
}
std::string OverloadSet::explain() {
    std::stringstream strstr;
    strstr << this;
    return "OverloadSet " + strstr.str();
}

void OverloadSet::IssueResolutionError(std::vector<Type*> types) {
    throw std::runtime_error("Overload resolution failure.");
}