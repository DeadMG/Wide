#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/ClangTU.h>
#include <array>
#include <sstream>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/Function.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>

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

OverloadSet::OverloadSet(AST::FunctionOverloadSet* s, Type* mem) {
	for(auto x : s->functions)
		functions.insert(x);
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
    } else {
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
}
Expression OverloadSet::BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) {
    std::vector<AST::Function*> ViableCandidates;

    if (nonstatic) {
        e = e.t->BuildValue(e, a);
        e.t = nonstatic;
        e.Expr = a.gen->CreateFieldExpression(e.Expr, 0);
        args.insert(args.begin(), e);
    }

    // Really badly named
    auto skipfirst = [&](AST::Function* x) {
        return nonstatic && (x->args.size() < 1 || x->args.front().name != "this");
    };
    for(auto x : functions) {
        if (nonstatic && skipfirst(x)) {
            if (x->args.size() + 1 == args.size())
                ViableCandidates.push_back(x);
        } else {
            if (x->args.size() == args.size())
                ViableCandidates.push_back(x);
        }
    }

    if (ViableCandidates.size() == 0)
        throw std::runtime_error("Attempted to call a function, but there were none with the right amount of arguments.");

    auto rank = std::vector<Function*>();
    auto best_rank = ConversionRank::None;
    for(auto f : ViableCandidates) {
        auto types = std::vector<Type*>();
        auto curr_rank = ConversionRank::Zero;
        for(std::size_t i = 0; i < args.size(); ++i) {
            if (skipfirst(f) && i == 0) continue;
            auto fi = skipfirst(f) ? i - 1 : i;
            if (f->args[fi].type) {
                auto con = f->higher;
                while(!dynamic_cast<AST::Module*>(con))
                    con = con->higher;
                struct LookupType : Type {
                    AST::DeclContext* con;
                    Type* autotype;
                    Type* nonstatic;
					Wide::Util::optional<Expression> AccessMember(Expression self, std::string name, Analyzer& a) {
                        if (name == "this")
                            return a.GetConstructorType(nonstatic)->BuildValueConstruction(std::vector<Expression>(), a);
                        if (name == "auto")
                            return a.GetConstructorType(autotype->Decay())->BuildValueConstruction(std::vector<Expression>(), a);
                        return a.GetDeclContext(con)->AccessMember(self, std::move(name), a);
                    }
                };
                LookupType lt;
                lt.con = con;
                lt.autotype = args[i].t;
                lt.nonstatic = nonstatic;
                auto argty = a.AnalyzeExpression(&lt, f->args[fi].type).t;
                if (auto con = dynamic_cast<ConstructorType*>(argty))
                    argty = con->GetConstructedType();
                else
                    throw std::runtime_error("The expression for a function argument must be a type.");

                // Prevent move constructors matching- so if T(T&&), T(U), T(T(U)) should not match as well as T(U)
                // Also, if the argument is T&& or T&, prevent others matching.
                if (nonstatic 
                    && f->name == "type"
                    && args.size() == 2
                    && (argty->IsReference(nonstatic->Decay()) || args[i].t->IsReference(nonstatic->Decay()))) 
                {
                    curr_rank = argty == args[i].t ? ConversionRank::Zero : ConversionRank::None;
                } else {
                    curr_rank = std::max(curr_rank, a.RankConversion(args[i].t, argty));
                }
                types.push_back(argty);
            } else {
                types.push_back(args[i].t->Decay());
            }
        }
        auto udt = nonstatic ? debug_cast<UserDefinedType>(nonstatic->Decay()) : nullptr;
        if (curr_rank < best_rank) {
            auto x = a.GetWideFunction(f, udt , types);
            rank.clear();
            rank.push_back(x);
            best_rank = curr_rank;
            continue;
        }
        if (curr_rank == best_rank && best_rank != ConversionRank::None) {
            auto x = a.GetWideFunction(f, udt, types);
            rank.push_back(x);
        }
    }
    
    if (rank.size() == 1) {
        auto call = rank[0]->BuildCall(e, std::move(args), a);
        if (e.Expr)
            call.Expr = a.gen->CreateChainExpression(e.Expr, call.Expr);
        return call;
    }

    if (rank.size() == 0)
        throw std::runtime_error("Attempted to call a function overload set, but there were no matches.");

    throw std::runtime_error("Attempted to call a function, but the call was ambiguous- there was more than one function of the correct ranking.");
}

ConversionRank OverloadSet::ResolveOverloadRank(std::vector<Type*> args, Analyzer& a) {
    std::vector<AST::Function*> ViableCandidates;

    if (nonstatic) {
        args.insert(args.begin(), a.AsLvalueType(nonstatic));
    }

    for(auto x : functions) {
        if (nonstatic) {
            if (x->args.size() + 1 == args.size())
                ViableCandidates.push_back(x);
        } else {
            if (x->args.size() == args.size())
                ViableCandidates.push_back(x);
        }
    }

    if (ViableCandidates.size() == 0)
        return ConversionRank::None;

    // We need slightly different rules for constructors.
    auto rank = std::vector<Function*>();
    auto best_rank = ConversionRank::None;
    for(auto f : ViableCandidates) {
        auto types = std::vector<Type*>();
        auto curr_rank = ConversionRank::Zero;
        for(std::size_t i = 0; i < args.size(); ++i) {
            if (nonstatic && i == 0) continue;
            auto fi = nonstatic ? i - 1 : i;
            if (f->args[fi].type) {
                auto con = f->higher;
                while(!dynamic_cast<AST::Module*>(con))
                    con = con->higher;
                struct LookupType : Type {
                    AST::DeclContext* con;
                    Type* nonstatic;
					Wide::Util::optional<Expression> AccessMember(Expression self, std::string name, Analyzer& a) {
                        if (name == "this") {
                            return a.GetConstructorType(nonstatic)->BuildValueConstruction(std::vector<Expression>(), a);
                        }
                        auto udt = debug_cast<UserDefinedType>(nonstatic->Decay());
                        if (udt->HasMember(name))
                            return udt->AccessMember(self, name, a);
                        return a.GetDeclContext(con)->AccessMember(self, std::move(name), a);
                    }
                };
                LookupType lt;
                lt.con = con;
                lt.nonstatic = nonstatic;
                auto argty = a.AnalyzeExpression(&lt, f->args[fi].type).t;
                if (auto con = dynamic_cast<ConstructorType*>(argty))
                    argty = con->GetConstructedType();
                else
                    throw std::runtime_error("The expression for a function argument must be a type.");

                // Prevent infinite recursion by disabling conversion checks on copy/move constructors
                if (nonstatic 
                    && f->name == "type"
                    && args.size() == 2) {
                    if (argty->IsReference(nonstatic->Decay())) {
                        curr_rank = argty == args[i] ? ConversionRank::Zero : ConversionRank::None;
                    } else {
                        curr_rank = std::max(ConversionRank::Two, a.RankConversion(args[i], argty));
                    }
                } else {
                    curr_rank = std::max(curr_rank, a.RankConversion(args[i], argty));
                }
                types.push_back(argty);
            } else {
                types.push_back(args[i]->Decay());
            }
        }
        auto x = a.GetWideFunction(f, debug_cast<UserDefinedType>(nonstatic->Decay()), types);
        if (curr_rank < best_rank) {
            rank.clear();
            rank.push_back(x);
            best_rank = curr_rank;
            continue;
        }
        if (curr_rank == best_rank && best_rank != ConversionRank::None) {
            rank.push_back(x);
        }
    }
    
    if (rank.size() == 1) {
        return best_rank;
    }

    return ConversionRank::None;
}

Codegen::Expression* OverloadSet::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an overload set from more than one argument.");
    if (args.size() == 1) {
		if (args[0].BuildValue(a).t == this)
			return a.gen->CreateStore(mem, args[0].BuildValue(a).Expr);
        if (nonstatic) {
            if (args[0].t->IsReference(nonstatic) || (nonstatic->IsReference() && nonstatic == args[0].t))
                return a.gen->CreateStore(a.gen->CreateFieldExpression(mem, 0), args[0].Expr);
            if (args[0].t == nonstatic)
                assert("Internal compiler error: Attempt to call a member function of a value.");
        }
        throw std::runtime_error("Can only construct overload set from another overload set of the same type, or a reference to T.");
    }
    if (nonstatic)
        throw std::runtime_error("Cannot default-construct a non-static overload set.");
    return a.gen->CreateNull(GetLLVMType(a));
}
clang::QualType OverloadSet::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    //if (nonstatic) throw std::runtime_error("Currently don't support Clang codegen for non-static overload sets.");

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
    for(auto&& x : functions) {
        // Instead of this, they will take a pointer to the recdecl here.
        // Wide member function types take "this" into account, whereas Clang ones do not.
        for(auto arg : x->args) if (!arg.type) continue;
        auto f = a.GetWideFunction(x, debug_cast<UserDefinedType>(nonstatic->Decay()));
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
	nonstatic = nullptr;
}