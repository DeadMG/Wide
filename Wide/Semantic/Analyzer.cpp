#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
#include <Wide/Parser/ASTVisitor.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/Void.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/MetaType.h>
#include <sstream>
#include <iostream>
#include <unordered_set>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

struct SemanticExpression : public AST::Expression {
    Semantic::Expression e;
    SemanticExpression(Semantic::Expression expr)
        : e(expr), AST::Expression(Lexer::Range()) {}
};

// After definition of type
Analyzer::~Analyzer() {}

Analyzer::Analyzer(const Options::Clang& opts, Codegen::Generator* g)    
    : clangopts(&opts)
    , gen(g)
    , null(nullptr)
{
    LiteralStringType = arena.Allocate<StringType>();
    struct NothingCall : public MetaType {
        Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
            return val;
        }
    };
    null = arena.Allocate<NullType>();
    NothingFunctor = arena.Allocate<NothingCall>();
}

void Analyzer::operator()(AST::Module* GlobalModule) {
    struct decltypetype : public MetaType { 
        Expression BuildCall(Expression obj, std::vector<Expression> args, Analyzer& a) {
            if (args.size() != 1)
                throw std::runtime_error("Attempt to call decltype with more or less than 1 argument.");

            if (auto con = dynamic_cast<ConstructorType*>(args[0].t->Decay())) {
                return a.GetConstructorType(args[0].t)->BuildValueConstruction(std::vector<Expression>(), a);
            }
            if (!dynamic_cast<LvalueType*>(args[0].t))
                args[0].t = a.GetRvalueType(args[0].t);
            return a.GetConstructorType(args[0].t)->BuildValueConstruction(std::vector<Expression>(), a);
        }
    };

    struct PointerCastType : public MetaType {
        Expression BuildCall(Expression obj, std::vector<Expression> args, Analyzer& a) {
            if (args.size() != 2)
                throw std::runtime_error("Attempted to call reinterpret_cast with a number of arguments that was not two.");
            auto conty = dynamic_cast<ConstructorType*>(args[0].t->Decay());
            if (!conty) throw std::runtime_error("Attempted to call reinterpret_cast with a first argument that was not a type.");
            if (!dynamic_cast<PointerType*>(conty->GetConstructedType())) throw std::runtime_error("Attempted to call reinterpret_cast with a first argument that was not a pointer type.");
            if (!dynamic_cast<PointerType*>(args[1].t->Decay())) throw std::runtime_error("Attempted to call reinterpret_cast with a second argument that was not of pointer type.");
            return Expression(conty->GetConstructedType(), args[1].Expr);
        }
    };

    struct MoveType : public MetaType {
        Expression BuildCall(Expression obj, std::vector<Expression> args, Analyzer& a) {
            if (args.size() != 1)
                throw std::runtime_error("Attempt to call move with a number of arguments that was not one.");
            return Expression(a.GetRvalueType(args[0].t->Decay()), args[0].Expr);
        }
    };
    
    GetWideModule(GlobalModule)->AddSpecialMember("cpp", Expression(arena.Allocate<ClangIncludeEntity>(), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("void", Expression(GetConstructorType(Void = arena.Allocate<VoidType>()), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("global", Expression(GetWideModule(GlobalModule), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("int8", GetConstructorType(GetIntegralType(8, true))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("uint8", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("int16", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("uint16", GetConstructorType(GetIntegralType(16, false))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("int32", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("uint32", GetConstructorType(GetIntegralType(32, false))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("int64", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("uint64", GetConstructorType(GetIntegralType(64, false))->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("bool", Expression(GetConstructorType(Boolean = arena.Allocate<Bool>()), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("true", Expression(Boolean, gen->CreateIntegralExpression(1, false, Boolean->GetLLVMType(*this))));
    GetWideModule(GlobalModule)->AddSpecialMember("false", Expression(Boolean, gen->CreateIntegralExpression(0, false, Boolean->GetLLVMType(*this))));
    GetWideModule(GlobalModule)->AddSpecialMember("decltype", arena.Allocate<decltypetype>()->BuildValueConstruction(std::vector<Expression>(), *this));

    GetWideModule(GlobalModule)->AddSpecialMember("byte", *GetWideModule(GlobalModule)->AccessMember(Expression(), "uint8", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("int", *GetWideModule(GlobalModule)->AccessMember(Expression(), "int32", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("short", *GetWideModule(GlobalModule)->AccessMember(Expression(), "int16", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("long", *GetWideModule(GlobalModule)->AccessMember(Expression(), "int64", *this));

    GetWideModule(GlobalModule)->AddSpecialMember("null", GetNullType()->BuildValueConstruction(std::vector<Expression>(), *this));
    GetWideModule(GlobalModule)->AddSpecialMember("reinterpret_cast", arena.Allocate<PointerCastType>()->BuildValueConstruction(std::vector<Expression>(), *this));
    //GetWideModule(GlobalModule)->AddSpecialMember("move", arena.Allocate<MoveType>()->BuildValueConstruction(std::vector<Expression>(), *this));
      
    GetWideModule(GlobalModule)->AccessMember(Expression(), "Standard", *this)->t->AccessMember(Expression(), "Main", *this)->t->BuildCall(Expression(), std::vector<Expression>(), *this);
    for(auto&& x : this->headers)
		gen->AddClangTU([&](llvm::Module* main) { x.second.GenerateCodeAndLinkModule(main); });
}

Expression Analyzer::AnalyzeExpression(Type* t, AST::Expression* e) {
    if (auto semexpr = dynamic_cast<SemanticExpression*>(e)) {
        return semexpr->e;
    }

	struct AnalyzerVisitor : public AST::Visitor<AnalyzerVisitor> {
		Type* t;
		Analyzer* self;
		Expression out;

		void VisitString(AST::StringExpr* str) {
			out.t = self->LiteralStringType;
			out.Expr = self->gen->CreateStringExpression(str->val);
		}
		void VisitMemberAccess(AST::MemAccessExpr* access) {
            auto val = self->AnalyzeExpression(t, access->expr);
			auto mem = val.AccessMember(access->mem, *self);
			if (!mem) throw std::runtime_error("Attempted to access a member that did not exist.");
			out = *mem;
		}
		void VisitCall(AST::FunctionCallExpr* funccall) {
            auto fun = self->AnalyzeExpression(t, funccall->callee);
            std::vector<Expression> args;
            for(auto&& arg : funccall->args) {
                args.push_back(self->AnalyzeExpression(t, arg));
            }
		    
            out = fun.BuildCall(std::move(args), *self);
		}
		void VisitIdentifier(AST::IdentifierExpr* ident) {
            while (auto udt = dynamic_cast<UserDefinedType*>(t))
                t = self->GetDeclContext(udt->GetDeclContext()->higher);
			auto mem = t->AccessMember(Expression(), ident->val, *self);
			if (!mem) throw std::runtime_error("Attempted to access a member that did not exist.");
			out = *mem;
		}
		void VisitBinaryExpression(AST::BinaryExpression* bin) {
			auto lhs = self->AnalyzeExpression(t, bin->lhs);
			auto rhs = self->AnalyzeExpression(t, bin->rhs);
			out = lhs.BuildBinaryExpression(rhs, bin->type, *self);
		}
		void VisitMetaCall(AST::MetaCallExpr* mcall) {
            auto fun = self->AnalyzeExpression(t, mcall->callee);
            std::vector<Expression> args;
            for(auto&& arg : mcall->args)
                args.push_back(self->AnalyzeExpression(t, arg));
		    
            out = fun.BuildMetaCall(std::move(args), *self);
		}
		void VisitInteger(AST::IntegerExpression* integer) {
            out = Expression( self->GetIntegralType(64, true), self->gen->CreateIntegralExpression(std::stoll(integer->integral_value), true, self->GetIntegralType(64, true)->GetLLVMType(*self)));
		}
		void VisitThisExpression(AST::ThisExpression*) {
			auto mem = t->AccessMember(Expression(), "this", *self);
			if (!mem) throw std::runtime_error("Attempted to access \"this\", but it was not found, probably because you were not in a member function.");
			out = *mem;
		}
		void VisitNegate(AST::NegateExpression* ne) {
            auto expr = self->AnalyzeExpression(t, ne->ex);
            expr.Expr = self->gen->CreateNegateExpression(expr.BuildBooleanConversion(*self));
            expr.t = self->Boolean;
            out = expr;
		}
        // Ugly to perform an AST-level transformation in the analyzer
        // But hey- the AST exists to represent the exact source.
		void VisitLambda(AST::Lambda* lam) {
            auto context = t->GetDeclContext()->higher;
            while(auto udt = dynamic_cast<AST::Type*>(context))
                context = udt->higher;
            auto ty = self->arena.Allocate<AST::Type>(context, "", lam->location);
            auto ovr = self->arena.Allocate<AST::FunctionOverloadSet>("()", ty);
            auto fargs = lam->args;
            auto fun = self->arena.Allocate<AST::Function>("()", lam->statements, std::vector<AST::Statement*>(), lam->location, std::move(fargs), ty, std::vector<AST::VariableStatement*>());
            ovr->functions.push_back(fun);
            ty->Functions["()"] = ovr;
		    
            // Need to not-capture things that would be available anyway.
            
            std::vector<std::unordered_set<std::string>> lambda_locals;
            // Only implicit captures.
            std::unordered_set<std::string> captures;
            struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
                std::vector<std::unordered_set<std::string>>* lambda_locals;
                std::unordered_set<std::string>* captures;
                void VisitVariableStatement(AST::VariableStatement* v) {
                    lambda_locals->back().insert(v->name);
                }
                void VisitLambdaCapture(AST::VariableStatement* v) {
                    lambda_locals->back().insert(v->name);
                }
                void VisitLambdaArgument(AST::FunctionArgument* arg) {
                    lambda_locals->back().insert(arg->name);
                }
                void VisitLambda(AST::Lambda* l) {
                    lambda_locals->emplace_back();
                    for(auto&& x : l->args)
                        VisitLambdaArgument(&x);
                    for(auto&& x : l->Captures)
                        VisitLambdaCapture(x);
                    lambda_locals->emplace_back();
                    for(auto&& x : l->statements)
                        VisitStatement(x);
                    lambda_locals->pop_back();
                    lambda_locals->pop_back();
                }
                void VisitIdentifier(AST::IdentifierExpr* e) {
                    for(auto&& scope : *lambda_locals)
                        if (scope.find(e->val) != scope.end())
                            return;
                    captures->insert(e->val);
                }
                void VisitCompoundStatement(AST::CompoundStatement* cs) {
                    lambda_locals->emplace_back();
                    for(auto&& x : cs->stmts)
                        VisitStatement(x);
                    lambda_locals->pop_back();
                }
                void VisitWhileStatement(AST::WhileStatement* wh) {
                    lambda_locals->emplace_back();
                    VisitExpression(wh->condition);
                    VisitStatement(wh->body);
                    lambda_locals->pop_back();
                }
                void VisitIfStatement(AST::IfStatement* br) {
                    lambda_locals->emplace_back();
                    VisitExpression(br->condition);
                    lambda_locals->emplace_back();
                    VisitStatement(br->true_statement);
                    lambda_locals->pop_back();
                    lambda_locals->pop_back();
                    lambda_locals->emplace_back();
                    VisitStatement(br->false_statement);
                    lambda_locals->pop_back();
                }
            };
            LambdaVisitor l;
            l.captures = &captures;
            l.lambda_locals = &lambda_locals;
            l.VisitLambda(lam);
                    
            // We obviously don't want to capture module-scope names.
            // Only capture from the local scope, and from "this".
            auto caps = std::move(captures);
            for(auto&& name : caps) {
                if (auto fun = dynamic_cast<Function*>(t)) {
                    if (fun->HasLocalVariable(name))
                        captures.insert(name);
                    if (auto udt = fun->IsMember()) {
                        if (udt->HasMember(name))
                            captures.insert(name);
                    }
                }
            }
		    
            // Just as a double-check, eliminate all explicit captures from the list. This should never have any effect
            // but I'll hunt down any bugs caused by eliminating it later.
            for(auto&& arg : lam->Captures)
                caps.erase(arg->name);
            std::vector<AST::VariableStatement*> initializers;
            std::vector<AST::FunctionArgument> funargs;
            std::vector<Expression> args;
            unsigned num = 0;
            for(auto&& arg : lam->Captures) {
                std::stringstream str;
                str << "__param" << num;
                auto init = self->AnalyzeExpression(t, arg->initializer);
                AST::FunctionArgument f;
                f.name = str.str();
                f.type = self->arena.Allocate<SemanticExpression>(Expression(self->GetConstructorType(init.t), nullptr));
                ty->variables.push_back(self->arena.Allocate<AST::VariableStatement>(arg->name, self->arena.Allocate<SemanticExpression>(Expression(self->GetConstructorType(init.t->Decay()), nullptr)), Lexer::Range()));
                initializers.push_back(self->arena.Allocate<AST::VariableStatement>(arg->name, self->arena.Allocate<AST::IdentifierExpr>(str.str(), Lexer::Range()), Lexer::Range()));
                funargs.push_back(f);
                ++num;
                args.push_back(init);
            }
            for(auto&& name : captures) {
                std::stringstream str;
                str << "__param" << num;
                auto capty = t->AccessMember(Expression(), name, *self)->t;
                initializers.push_back(self->arena.Allocate<AST::VariableStatement>(name, self->arena.Allocate<AST::IdentifierExpr>(str.str(), Lexer::Range()), Lexer::Range()));
                AST::FunctionArgument f;
                f.name = str.str();
                f.type = self->arena.Allocate<SemanticExpression>(Expression(self->GetConstructorType(capty), nullptr));
                funargs.push_back(f);
                if (!lam->defaultref)
                    capty = capty->Decay();
                ty->variables.push_back(self->arena.Allocate<AST::VariableStatement>(name, self->arena.Allocate<SemanticExpression>(Expression(self->GetConstructorType(capty), nullptr)), Lexer::Range()));
                ++num;
                args.push_back(*t->AccessMember(Expression(), name, *self));        
            }
            auto conoverset = self->arena.Allocate<AST::FunctionOverloadSet>("type", ty);
            conoverset->functions.push_back(self->arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(funargs), ty, std::move(initializers)));
            ty->Functions["type"] = conoverset;
            auto lamty = self->GetUDT(ty, t);
            self->AddCopyConstructor(ty, lamty);
            self->AddMoveConstructor(ty, lamty);
            auto obj = lamty->BuildRvalueConstruction(std::move(args), *self);
            out = obj;
		}
		void VisitDereference(AST::DereferenceExpression* deref) {
            out = self->AnalyzeExpression(t, deref->ex).BuildDereference(*self);
		}
		void VisitIncrement(AST::Increment* inc) {
            auto lhs = self->AnalyzeExpression(t, inc->ex);
			out = lhs.BuildIncrement(inc->postfix, *self);
		}
		void VisitType(AST::Type* ty) {
            ty->higher = t->GetDeclContext();
            auto udt = self->GetUDT(ty, t);
            out = self->GetConstructorType(udt)->BuildValueConstruction(*self);
		}
		void VisitPointerAccess(AST::PointerAccess* ptr) {
			auto mem = self->AnalyzeExpression(t, ptr->ex).PointerAccessMember(ptr->member, *self);
            if (!mem)
				throw std::runtime_error("Attempted to access a member of a pointer, but it contained no such member.");
			out = *mem;
		}
		void VisitAddressOf(AST::AddressOfExpression* add) {
			out = self->AnalyzeExpression(t, add->ex).AddressOf(*self);
		}
	};

	AnalyzerVisitor v;
	v.self = this;
	v.t = t;
	v.VisitExpression(e);
	return v.out;
}

ClangUtil::ClangTU* Analyzer::LoadCPPHeader(std::string file) {
    if (headers.find(file) != headers.end())
        return &headers.find(file)->second;
	headers.insert(std::make_pair(file, ClangUtil::ClangTU(gen->GetContext(), file, *clangopts)));
    auto ptr = &headers.find(file)->second;
    return ptr;
}

Type* Analyzer::GetClangType(ClangUtil::ClangTU& from, clang::QualType t) {
    t = t.getCanonicalType();
    if (t.isConstQualified())
       t.removeLocalConst();
    if (t->isLValueReferenceType()) {
        return GetLvalueType(GetClangType(from, t->getAs<clang::LValueReferenceType>()->getPointeeType()));
    }
    if (t->isRValueReferenceType()) {
        return GetRvalueType(GetClangType(from, t->getAs<clang::RValueReferenceType>()->getPointeeType()));
    }
    if (t->isBooleanType())
        return Boolean;
    if (t->isIntegerType())
        return GetIntegralType(from.GetASTContext().getIntWidth(t), t->isSignedIntegerType());
    if (t->isVoidType())
        return Void;
    if (t->isPointerType()) {
        auto pt = t->getPointeeType();
        if (pt->isCharType())
            return LiteralStringType;
        return GetPointerType(GetClangType(from, pt));
    }
    if (t->isBooleanType())
        return Boolean;
    if (ClangTypes.find(t) != ClangTypes.end())
        return ClangTypes[t];
    return ClangTypes[t] = arena.Allocate<ClangType>(&from, t);
}

ClangNamespace* Analyzer::GetClangNamespace(ClangUtil::ClangTU& tu, clang::DeclContext* con) {
    if (ClangNamespaces.find(con) != ClangNamespaces.end())
        return ClangNamespaces[con];
    return ClangNamespaces[con] = arena.Allocate<ClangNamespace>(con, &tu);
}

std::size_t VectorTypeHasher::operator()(const std::vector<Type*>& t) const {
    std::size_t hash = 0;
    for(unsigned index = 0; index < t.size(); index++) {
#pragma warning(disable : 4244)
        hash += pow(31u, index) * std::hash<Type*>()(t[index]);
#pragma warning(default : 4244)
    }
    return hash;
}

FunctionType* Analyzer::GetFunctionType(Type* ret, const std::vector<Type*>& t) {
    if (FunctionTypes[ret].find(t) != FunctionTypes[ret].end()) {
        return FunctionTypes[ret][t];
    }
    return FunctionTypes[ret][t] = arena.Allocate<FunctionType>(ret, t);
}

Function* Analyzer::GetWideFunction(AST::Function* p, UserDefinedType* nonstatic, const std::vector<Type*>& types) {
    if (WideFunctions.find(p) != WideFunctions.end())
        if (WideFunctions[p].find(types) != WideFunctions[p].end())
            return WideFunctions[p][types];
    return WideFunctions[p][types] = arena.Allocate<Function>(types, p, *this, nonstatic);
}

Module* Analyzer::GetWideModule(AST::Module* p) {
    if (WideModules.find(p) != WideModules.end())
        return WideModules[p];
    auto mod = WideModules[p] = arena.Allocate<Module>(p);
    DeclContexts[p] = mod;
    return mod;
}

LvalueType* Analyzer::GetLvalueType(Type* t) {
    if (t == Void)
        throw std::runtime_error("Can't get an lvalue ref to void.");

    if (LvalueTypes.find(t) != LvalueTypes.end())
        return LvalueTypes[t];

    // Prefer hash lookup to dynamic_cast.
    if (auto lval = dynamic_cast<LvalueType*>(t)) {
        return LvalueTypes[t] = lval;
    }
    
    // This implements "named rvalue ref is an lvalue", and static_cast<T&&>(T&).
    // by permitting T&& & to become T&.
    if (auto rval = dynamic_cast<RvalueType*>(t)) {
        return LvalueTypes[t] = GetLvalueType(rval->Decay());
    }

    return LvalueTypes[t] = arena.Allocate<LvalueType>(t);
}

Type* Analyzer::GetRvalueType(Type* t) {    
    if (t == Void)
        throw std::runtime_error("Can't get an rvalue ref to void.");
    
    if (RvalueTypes.find(t) != RvalueTypes.end())
        return RvalueTypes[t];
    
    // Prefer hash lookup to dynamic_cast.
    if (auto rval = dynamic_cast<RvalueType*>(t))
        return RvalueTypes[t] = rval;

    if (auto lval = dynamic_cast<LvalueType*>(t))
        return RvalueTypes[t] = lval;

    return RvalueTypes[t] = arena.Allocate<RvalueType>(t);
}

ConstructorType* Analyzer::GetConstructorType(Type* t) {
    if (ConstructorTypes.find(t) != ConstructorTypes.end())
        return ConstructorTypes[t];
    return ConstructorTypes[t] = arena.Allocate<ConstructorType>(t);
}

ClangTemplateClass* Analyzer::GetClangTemplateClass(ClangUtil::ClangTU& from, clang::ClassTemplateDecl* decl) {
    if (ClangTemplateClasses.find(decl) != ClangTemplateClasses.end())
        return ClangTemplateClasses[decl];
    return ClangTemplateClasses[decl] = arena.Allocate<ClangTemplateClass>(decl, &from);
}

OverloadSet* Analyzer::GetOverloadSet(AST::FunctionOverloadSet* set, Type* t) {
    if (t)
        assert(dynamic_cast<UserDefinedType*>(t->Decay()));
    if (OverloadSets.find(set) != OverloadSets.end())
        if (OverloadSets[set].find(t) != OverloadSets[set].end())
            return OverloadSets[set][t];
    return OverloadSets[set][t] = arena.Allocate<OverloadSet>(set, t);
}
void Analyzer::AddClangType(clang::QualType t, Type* match) {
    if (ClangTypes.find(t) != ClangTypes.end())
        throw std::runtime_error("Attempt to AddClangType on a type that already had a Clang type.");
    ClangTypes[t] = match;
}

UserDefinedType* Analyzer::GetUDT(AST::Type* t, Type* context) {
    if (UDTs.find(t) != UDTs.end())
        if (UDTs[t].find(context) != UDTs[t].end())
            return UDTs[t][context];
    auto ty = UDTs[t][context] = arena.Allocate<UserDefinedType>(t, *this, context);
    return ty;
}

Type* Analyzer::GetDeclContext(AST::DeclContext* con) {
    if (DeclContexts.find(con) != DeclContexts.end())
        return DeclContexts[con];
    if (auto mod = dynamic_cast<AST::Module*>(con))
        return DeclContexts[con] = GetWideModule(mod);
	return GetDeclContext(con->higher);
}

ConversionRank Analyzer::RankConversion(Type* from, Type* to) {
    //          T T& T&& to
    //    T     0  x  0
    //    T&    0  0  x
    //    T&&   0  x  0
    //    U     2  x  2
    //    from

    if (from == to) return ConversionRank::Zero;        
    //          T T& T&& to
    //    T        x  0
    //    T&    0     x
    //    T&&   0  x  
    //    U     2  x  2
    //    from

    if (auto rval = dynamic_cast<RvalueType*>(to)) {
        if (rval->Decay() == from) {
            return ConversionRank::Zero;
        }
    }
    //          T T& T&& to
    //    T        x  
    //    T&    0     x
    //    T&&   0  x  
    //    U     2  x  2
    //    from

    // Since we just covered the only valid T& case, if T& then fail
    if (auto lval = dynamic_cast<LvalueType*>(to))
        return ConversionRank::None;    
    //          T T& T&& to
    //    T           
    //    T&    0     x
    //    T&&   0      
    //    U     2     2
    //    from

    if (auto rval = dynamic_cast<RvalueType*>(to))
        if (auto lval = dynamic_cast<LvalueType*>(from))
            if (lval->IsReference() == rval->IsReference())
                return ConversionRank::None;
    //          T            T&&       U
    //    T           
    //    T&    copyable    
    //    T&&   movable      
    //    U     convertible  U to T
    //    from

    // The only remaining cases are type-specific, not guaranteed for all types.
    // Ask "to" to handle it.
    return to->RankConversionFrom(from, *this);
}

void Analyzer::AddCopyConstructor(AST::Type* t, UserDefinedType* ty) {
    // If non-complex, we just use a load/store, no need for explicit.
    if (!ty->IsComplexType()) return;

    /*if (!ty->IsComplexType()) {
        // Generate a simple copy.
        std::vector<AST::FunctionArgument> args;
        AST::FunctionArgument other;
        other.name = "other";
        other.type = arena.Allocate<SemanticExpression>(Expression(GetConstructorType(GetLvalueType(ty)), nullptr));
        args.push_back(other);        
        std::vector<AST::VariableStatement*> initializers;
        std::vector<AST::Statement*> body;
        body.push_back(Expression(nullptr, gen->CreateStore(gen->CreateParameterExpression(0), gen->CreateLoad(gen->CreateParameterExpression(1)))));
        
    }*/
    auto members = ty->GetMembers();
    auto should = true;
    for(auto&& m : members) {
        should = should && ((RankConversion(GetLvalueType(m.t), m.t) == ConversionRank::Zero) || !m.t->IsComplexType());
    }
    if (!should) return;
    std::vector<AST::FunctionArgument> args;
    AST::FunctionArgument self;
    self.name = "other";
    self.type = arena.Allocate<SemanticExpression>(Expression(GetConstructorType(GetLvalueType(ty)), nullptr));
    args.push_back(self);

    std::vector<AST::VariableStatement*> initializers;
    for(auto&& m : members) {
        initializers.push_back(
            arena.Allocate<AST::VariableStatement>(m.name, arena.Allocate<AST::MemAccessExpr>(m.name, arena.Allocate<AST::IdentifierExpr>("other", Lexer::Range()), Lexer::Range()), Lexer::Range())
        );
    }
    if (t->Functions.find("type") == t->Functions.end())
        t->Functions["type"] = arena.Allocate<AST::FunctionOverloadSet>("type", t);
    t->Functions["type"]->functions.push_back(arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(args), t, std::move(initializers)));
}

void Analyzer::AddMoveConstructor(AST::Type* t, UserDefinedType* ty) {

    // If non-complex, we just use a load/store, no need for explicit.
    if (!ty->IsComplexType()) return;

    auto members = ty->GetMembers();
    auto should = true;
    for(auto&& m : members) {
        should = should && ((RankConversion(GetRvalueType(m.t), m.t) == ConversionRank::Zero)|| !m.t->IsComplexType());
    }
    if (!should) return;
    std::vector<AST::FunctionArgument> args;
    AST::FunctionArgument self;
    self.name = "other";
    self.type = arena.Allocate<SemanticExpression>(Expression(GetConstructorType(GetRvalueType(ty)), nullptr));
    args.push_back(self);

    std::vector<AST::VariableStatement*> initializers;
    auto other = Expression(GetRvalueType(ty), gen->CreateParameterExpression(1));
    for(auto&& m : members) {
        initializers.push_back(arena.Allocate<AST::VariableStatement>(m.name, arena.Allocate<SemanticExpression>(*other.t->AccessMember(other, m.name, *this)), Lexer::Range()));
    }
    if (t->Functions.find("type") == t->Functions.end())
        t->Functions["type"] = arena.Allocate<AST::FunctionOverloadSet>("type", t);
    t->Functions["type"]->functions.push_back(arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(args), t, std::move(initializers)));
}

void Analyzer::AddDefaultConstructor(AST::Type* t, UserDefinedType* ty) {
    auto members = ty->GetMembers();
    /*auto should = true;
    for(auto&& m : members) {
        should = should && m.t
    }
    if (!should) return;*/
    
    std::vector<AST::FunctionArgument> args;
    std::vector<AST::VariableStatement*> initializers;
    /*for(auto&& m : members) {
        initializers.push_back(arena.Allocate<AST::VariableStatement>(m.name, nullptr, Lexer::Range()));
    }*/
    if (t->Functions.find("type") == t->Functions.end())
        t->Functions["type"] = arena.Allocate<AST::FunctionOverloadSet>("type", t);
    t->Functions["type"]->functions.push_back(arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(args), t, std::move(initializers)));
}
IntegralType* Analyzer::GetIntegralType(unsigned bits, bool sign) {
    if (integers.find(bits) != integers.end())
        if (integers[bits].find(sign) != integers[bits].end())
            return integers[bits][sign];
    return integers[bits][sign] = arena.Allocate<IntegralType>(bits, sign);
}
PointerType* Analyzer::GetPointerType(Type* to) {
    if (Pointers.find(to) != Pointers.end())
        return Pointers[to];
    return Pointers[to] = arena.Allocate<PointerType>(to);
}
Type* Analyzer::GetNullType() {
    return null;
}
Type* Analyzer::GetBooleanType() {
    return Boolean;
}
Type* Analyzer::GetVoidType() {
    return Void;
}
Type* Analyzer::GetNothingFunctorType() {
    return NothingFunctor;
}
Type* Analyzer::GetLiteralStringType() {
    return LiteralStringType;
}

Type* Analyzer::AsRvalueType(Type* t) {
	return GetRvalueType(t);
}
Type* Analyzer::AsLvalueType(Type* t) {
	return GetLvalueType(t);
}
bool Analyzer::IsLvalueType(Type* t) {
	return dynamic_cast<LvalueType*>(t);
}
bool Analyzer::IsRvalueType(Type* t) {
	return dynamic_cast<RvalueType*>(t);
}
OverloadSet* Analyzer::GetOverloadSet(OverloadSet* f, OverloadSet* s) {
	if (CombinedOverloadSets[f].find(s) != CombinedOverloadSets[f].end())
		return CombinedOverloadSets[f][s];
	return CombinedOverloadSets[f][s] = arena.Allocate<OverloadSet>(f, s);
}