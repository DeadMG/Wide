#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
#include <Wide/Parser/ASTVisitor.h>
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
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/SemanticExpression.h>
#include <Wide/Semantic/FloatType.h>
#include <sstream>
#include <iostream>
#include <unordered_set>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

// After definition of type
Analyzer::~Analyzer() {}

Analyzer::Analyzer(const Options::Clang& opts, Codegen::Generator& g, const AST::Module* GlobalModule)    
    : clangopts(&opts)
    , gen(&g)
    , null(nullptr)
{
    LiteralStringType = arena.Allocate<StringType>();
    struct NothingCall : public MetaType {
        Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override {
            return val;
        }
    };
    null = arena.Allocate<NullType>();
    NothingFunctor = arena.Allocate<NothingCall>();
    struct decltypetype : public MetaType { 
        Expression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override {
            if (args.size() != 1)
                throw std::runtime_error("Attempt to call decltype with more or less than 1 argument.");

            if (auto con = dynamic_cast<ConstructorType*>(args[0].t->Decay())) {
                return c->GetConstructorType(args[0].t)->BuildValueConstruction(c);
            }
            if (!dynamic_cast<LvalueType*>(args[0].t))
                args[0].t = c->GetRvalueType(args[0].t);
            return c->GetConstructorType(args[0].t)->BuildValueConstruction(c);
        }
    };

    struct PointerCastType : public MetaType {
        Expression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override {
            if (args.size() != 2)
                throw std::runtime_error("Attempted to call reinterpret_cast with a number of arguments that was not two.");
            auto conty = dynamic_cast<ConstructorType*>(args[0].t->Decay());
            if (!conty) throw std::runtime_error("Attempted to call reinterpret_cast with a first argument that was not a type.");
            if (!dynamic_cast<PointerType*>(conty->GetConstructedType())) throw std::runtime_error("Attempted to call reinterpret_cast with a first argument that was not a pointer type.");
            if (!dynamic_cast<PointerType*>(args[1].t->Decay())) throw std::runtime_error("Attempted to call reinterpret_cast with a second argument that was not of pointer type.");
            return ConcreteExpression(conty->GetConstructedType(), args[1].Expr);
        }
    };

    struct MoveType : public MetaType {
        Expression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override {
            if (args.size() != 1)
                throw std::runtime_error("Attempt to call move with a number of arguments that was not one.");
            return ConcreteExpression(c->GetRvalueType(args[0].t->Decay()), args[0].Expr);
        }
    };

    static const auto location = Lexer::Range(std::make_shared<std::string>("Analyzer internal."));
    Context c(*this, location, [](ConcreteExpression e) {
        assert(false);
    });
    global = GetWideModule(GlobalModule, nullptr);
    auto global_val = global->BuildValueConstruction(c);
    EmptyOverloadSet = arena.Allocate<OverloadSet>(std::unordered_set<const AST::Function*>(), global);
    global->AddSpecialMember("cpp", ConcreteExpression(arena.Allocate<ClangIncludeEntity>(), nullptr));
    global->AddSpecialMember("void", ConcreteExpression(GetConstructorType(Void = arena.Allocate<VoidType>()), nullptr));
    global->AddSpecialMember("global", ConcreteExpression(global, nullptr));
    global->AddSpecialMember("int8", GetConstructorType(GetIntegralType(8, true))->BuildValueConstruction(c));
    global->AddSpecialMember("uint8", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(c));
    global->AddSpecialMember("int16", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(c));
    global->AddSpecialMember("uint16", GetConstructorType(GetIntegralType(16, false))->BuildValueConstruction(c));
    global->AddSpecialMember("int32", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(c));
    global->AddSpecialMember("uint32", GetConstructorType(GetIntegralType(32, false))->BuildValueConstruction(c));
    global->AddSpecialMember("int64", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(c));
    global->AddSpecialMember("uint64", GetConstructorType(GetIntegralType(64, false))->BuildValueConstruction(c));
    global->AddSpecialMember("float32", GetConstructorType(GetFloatType(32))->BuildValueConstruction(c));
    global->AddSpecialMember("float64", GetConstructorType(GetFloatType(64))->BuildValueConstruction(c));
    global->AddSpecialMember("bool", ConcreteExpression(GetConstructorType(Boolean = arena.Allocate<Bool>()), nullptr));
    global->AddSpecialMember("true", ConcreteExpression(Boolean, gen->CreateIntegralExpression(1, false, Boolean->GetLLVMType(*this))));
    global->AddSpecialMember("false", ConcreteExpression(Boolean, gen->CreateIntegralExpression(0, false, Boolean->GetLLVMType(*this))));
    global->AddSpecialMember("decltype", arena.Allocate<decltypetype>()->BuildValueConstruction(c));

    global->AddSpecialMember("byte",   *global->AccessMember(global_val, "uint8", c));
    global->AddSpecialMember("int",    *global->AccessMember(global_val, "int32", c));
    global->AddSpecialMember("short",  *global->AccessMember(global_val, "int16", c));
    global->AddSpecialMember("long",   *global->AccessMember(global_val, "int64", c));
    global->AddSpecialMember("float",  *global->AccessMember(global_val, "float32", c));
    global->AddSpecialMember("double", *global->AccessMember(global_val, "float64", c));

    global->AddSpecialMember("null", GetNullType()->BuildValueConstruction(c));
    global->AddSpecialMember("reinterpret_cast", arena.Allocate<PointerCastType>()->BuildValueConstruction(c));
    //GetWideModule(GlobalModule)->AddSpecialMember("move", arena.Allocate<MoveType>()->BuildValueConstruction(std::vector<Expression>(), *this));
}

Expression Analyzer::AnalyzeExpression(Type* t, const AST::Expression* e, std::function<void(ConcreteExpression)> handler) {
    if (auto semexpr = dynamic_cast<const SemanticExpression*>(e)) {
        return semexpr->e;
    }

    struct AnalyzerVisitor : public AST::Visitor<AnalyzerVisitor> {
        Type* t;
        Analyzer* self;
        Wide::Util::optional<Expression> out;
        std::function<void(ConcreteExpression)> handler;

        void VisitString(const AST::String* str) {
            out = ConcreteExpression(
                self->LiteralStringType,
                self->gen->CreateStringExpression(str->val)
            );
        }
        void VisitMemberAccess(const AST::MemberAccess* access) {
            auto val = self->AnalyzeExpression(t, access->expr, handler);
            auto mem = val.AccessMember(access->mem, Context(*self, access->location, handler));
            if (!mem) throw SemanticError(access->location, Error::NoMember);
            out = *mem;
        }
        void VisitCall(const AST::FunctionCall* funccall) {
            auto fun = self->AnalyzeExpression(t, funccall->callee, handler);
            std::vector<Expression> args;
            for(auto&& arg : funccall->args) {
                args.push_back(self->AnalyzeExpression(t, arg, handler));
            }
            
            out = fun.BuildCall(std::move(args), Context(*self, funccall->location, handler));
        }
        void VisitIdentifier(const AST::Identifier* ident) {
            auto mem = self->LookupIdentifier(t, ident);
            if (!mem) throw std::runtime_error("Attempted to access a member that did not exist.");
            out = *mem;
        }
        void VisitBinaryExpression(const AST::BinaryExpression* bin) {
            auto lhs = self->AnalyzeExpression(t, bin->lhs, handler);
            auto rhs = self->AnalyzeExpression(t, bin->rhs, handler);
            out = lhs.BuildBinaryExpression(rhs, bin->type, Context(*self, bin->location, handler));
        }
        void VisitMetaCall(const AST::MetaCall* mcall) {
            auto fun = self->AnalyzeExpression(t, mcall->callee, handler);
            std::vector<Expression> args;
            for(auto&& arg : mcall->args)
                args.push_back(self->AnalyzeExpression(t, arg, handler));
            
            out = fun.BuildMetaCall(std::move(args), Context(*self, mcall->location, handler));
        }
        void VisitInteger(const AST::Integer* integer) {
            out = ConcreteExpression( self->GetIntegralType(64, true), self->gen->CreateIntegralExpression(std::stoll(integer->integral_value), true, self->GetIntegralType(64, true)->GetLLVMType(*self)));
        }
        void VisitThis(const AST::This* loc) {
            auto fun = dynamic_cast<Function*>(t);
            if (!fun) throw std::runtime_error("Attempted to access \"this\" outside of a function.");            
            auto mem = fun->LookupLocal("this", Context(*self, loc->location, handler));
            if (!mem) throw std::runtime_error("Attempted to access \"this\", but it was not found, probably because you were not in a member function.");
            out = *mem;
        }
        void VisitNegate(const AST::Negate* ne) {
            out = self->AnalyzeExpression(t, ne->ex, handler).BuildNegate(Context(*self, ne->location, handler));
        }
        // Ugly to perform an AST-level transformation in the analyzer
        // But hey- the AST exists to represent the exact source.
        void VisitLambda(const AST::Lambda* lam) {
            auto ty = self->arena.Allocate<AST::Type>("__lambda", lam->location);
            auto ovr = self->arena.Allocate<AST::FunctionOverloadSet>();
            auto fargs = lam->args;
            auto fun = self->arena.Allocate<AST::Function>("()", lam->statements, std::vector<AST::Statement*>(), lam->location, std::move(fargs), std::vector<AST::Variable*>());
            ovr->functions.insert(fun);
            ty->opcondecls[Lexer::TokenType::OpenBracket] = ovr;
            
            // Need to not-capture things that would be available anyway.
            
            std::vector<std::unordered_set<std::string>> lambda_locals;
            // Only implicit captures.
            std::unordered_set<std::string> captures;
            struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
                std::vector<std::unordered_set<std::string>>* lambda_locals;
                std::unordered_set<std::string>* captures;
                void VisitVariableStatement(const AST::Variable* v) {
                    lambda_locals->back().insert(v->name);
                }
                void VisitLambdaCapture(const AST::Variable* v) {
                    lambda_locals->back().insert(v->name);
                }
                void VisitLambdaArgument(const AST::FunctionArgument* arg) {
                    lambda_locals->back().insert(arg->name);
                }
                void VisitLambda(const AST::Lambda* l) {
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
                void VisitIdentifier(const AST::Identifier* e) {
                    for(auto&& scope : *lambda_locals)
                        if (scope.find(e->val) != scope.end())
                            return;
                    captures->insert(e->val);
                }
                void VisitCompoundStatement(const AST::CompoundStatement* cs) {
                    lambda_locals->emplace_back();
                    for(auto&& x : cs->stmts)
                        VisitStatement(x);
                    lambda_locals->pop_back();
                }
                void VisitWhileStatement(const AST::While* wh) {
                    lambda_locals->emplace_back();
                    VisitExpression(wh->condition);
                    VisitStatement(wh->body);
                    lambda_locals->pop_back();
                }
                void VisitIfStatement(const AST::If* br) {
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
                    if (auto udt = dynamic_cast<UserDefinedType*>(fun->GetContext(*self))) {
                        if (udt->HasMember(name))
                            captures.insert(name);
                    }
                }
            }
            
            // Just as a double-check, eliminate all explicit captures from the list. This should never have any effect
            // but I'll hunt down any bugs caused by eliminating it later.
            for(auto&& arg : lam->Captures)
                caps.erase(arg->name);

            std::vector<Expression> cap_expressions;
            bool defer = false;
            for(auto&& arg : lam->Captures) {
                auto expr = self->AnalyzeExpression(t, arg->initializer, handler);
                if (expr.contents.type() == typeid(DeferredExpression))
                    defer = true;
                cap_expressions.push_back(expr);
            }
            for(auto&& name : captures) {                
                AST::Identifier ident(name, lam->location);
                auto expr = self->AnalyzeExpression(t, &ident, handler);
                if (expr.contents.type() == typeid(DeferredExpression))
                    defer = true;
                cap_expressions.push_back(expr);
            }

            auto lamself = self;
            auto lamt = t;
            auto handlercopy = handler;
            auto handler = handlercopy;
            auto process_lambda = [lam, cap_expressions, lamself, ty, lamt, captures, handler](Wide::Semantic::Type*) -> Wide::Semantic::ConcreteExpression {
                auto t = lamt;
                auto self = lamself;
                std::vector<AST::Variable*> initializers;
                std::vector<AST::FunctionArgument> funargs;
                std::vector<ConcreteExpression> args;
                unsigned num = 0;
                for(auto&& arg : lam->Captures) {
                    std::stringstream str;
                    str << "__param" << num;
                    auto init = cap_expressions[num].Resolve(nullptr);
                    AST::FunctionArgument f(lam->location);
                    f.name = str.str();
                    f.type = self->arena.Allocate<SemanticExpression>(ConcreteExpression(self->GetConstructorType(init.t), nullptr), lam->location);
                    ty->variables.push_back(self->arena.Allocate<AST::Variable>(arg->name, self->arena.Allocate<SemanticExpression>(ConcreteExpression(self->GetConstructorType(init.t->Decay()), nullptr), lam->location), lam->location));
                    initializers.push_back(self->arena.Allocate<AST::Variable>(arg->name, self->arena.Allocate<AST::Identifier>(str.str(), lam->location), lam->location));
                    funargs.push_back(f);
                    ++num;
                    args.push_back(init);
                }
                for(auto&& name : captures) {
                    std::stringstream str;
                    str << "__param" << num;
                    auto capty = cap_expressions[num].Resolve(nullptr).t;
                    initializers.push_back(self->arena.Allocate<AST::Variable>(name, self->arena.Allocate<AST::Identifier>(str.str(), lam->location), lam->location));
                    AST::FunctionArgument f(lam->location);
                    f.name = str.str();
                    f.type = self->arena.Allocate<SemanticExpression>(ConcreteExpression(self->GetConstructorType(capty), nullptr), lam->location);
                    funargs.push_back(f);
                    if (!lam->defaultref)
                        capty = capty->Decay();
                    ty->variables.push_back(self->arena.Allocate<AST::Variable>(name, self->arena.Allocate<SemanticExpression>(ConcreteExpression(self->GetConstructorType(capty), nullptr), lam->location), lam->location));
                    args.push_back(cap_expressions[num].Resolve(nullptr));
                    ++num;
                }
                auto conoverset = self->arena.Allocate<AST::FunctionOverloadSet>();
                conoverset->functions.insert(self->arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), lam->location, std::move(funargs), std::move(initializers)));
                ty->Functions["type"] = conoverset;
                auto lamty = self->GetUDT(ty, t);
                return lamty->BuildRvalueConstruction(std::move(args), Context(*self, lam->location, handler));
            };
            if (defer) {
                DeferredExpression d(std::move(process_lambda));
                out = d;
                return;
            }
            out = process_lambda(nullptr);
        }
        void VisitDereference(const AST::Dereference* deref) {
            out = self->AnalyzeExpression(t, deref->ex, handler).BuildDereference(Context(*self, deref->location, handler));
        }
        void VisitIncrement(const AST::Increment* inc) {
            auto lhs = self->AnalyzeExpression(t, inc->ex, handler);
            out = lhs.BuildIncrement(inc->postfix, Context(*self, inc->location, handler));
        }
        void VisitType(const AST::Type* ty) {
            auto udt = self->GetUDT(ty, t);
            out = self->GetConstructorType(udt)->BuildValueConstruction(Context(*self, ty->location, handler));
        }
        void VisitPointerAccess(const AST::PointerMemberAccess* ptr) {
            auto mem = self->AnalyzeExpression(t, ptr->ex, handler).PointerAccessMember(ptr->member, Context(*self, ptr->location, handler));
            if (!mem)
                throw std::runtime_error("Attempted to access a member of a pointer, but it contained no such member.");
            out = *mem;
        }
        void VisitAddressOf(const AST::AddressOf* add) {
            out = self->AnalyzeExpression(t, add->ex, handler).AddressOf(Context(*self, add->location, handler));
        }
    };

    AnalyzerVisitor v;
    v.self = this;
    v.t = t;
    v.handler = handler;
    v.VisitExpression(e);
    if (!v.out)
        assert(false && "ASTVisitor did not return an expression.");
    return *v.out;
}

ClangUtil::ClangTU* Analyzer::LoadCPPHeader(std::string file, Lexer::Range where) {
    if (headers.find(file) != headers.end())
        return &headers.find(file)->second;
    headers.insert(std::make_pair(file, ClangUtil::ClangTU(gen->GetContext(), file, *clangopts, where)));
    auto ptr = &headers.find(file)->second;
    gen->AddClangTU([=](llvm::Module* main) { ptr->GenerateCodeAndLinkModule(main); });
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
    if (t->isNullPtrType())
        return null;
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

Function* Analyzer::GetWideFunction(const AST::Function* p, Type* context, const std::vector<Type*>& types) {
    if (WideFunctions.find(p) != WideFunctions.end())
        if (WideFunctions[p].find(types) != WideFunctions[p].end())
            return WideFunctions[p][types];
    return WideFunctions[p][types] = arena.Allocate<Function>(types, p, *this, context);
}

Module* Analyzer::GetWideModule(const AST::Module* p, Module* higher) {
    if (WideModules.find(p) != WideModules.end())
        return WideModules[p];
    return WideModules[p] = arena.Allocate<Module>(p, higher);
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

OverloadSet* Analyzer::GetOverloadSet(const AST::FunctionOverloadSet* set, Type* t) {
    if (OverloadSets.find(set) != OverloadSets.end())
        if (OverloadSets[set].find(t) != OverloadSets[set].end())
            return OverloadSets[set][t];
    return OverloadSets[set][t] = arena.Allocate<OverloadSet>(set, t);
}
OverloadSet* Analyzer::GetOverloadSet() {
    return EmptyOverloadSet;
}
OverloadSet* Analyzer::GetOverloadSet(Callable* c) {
    std::unordered_set<Callable*> set;
    set.insert(c);
    if (callable_overload_sets.find(set) != callable_overload_sets.end())
        return callable_overload_sets[set];
    return callable_overload_sets[set] = arena.Allocate<OverloadSet>(set);
}
void Analyzer::AddClangType(clang::QualType t, Type* match) {
    if (ClangTypes.find(t) != ClangTypes.end())
        throw std::runtime_error("Attempt to AddClangType on a type that already had a Clang type.");
    ClangTypes[t] = match;
}

UserDefinedType* Analyzer::GetUDT(const AST::Type* t, Type* context) {
    if (UDTs.find(t) != UDTs.end())
        if (UDTs[t].find(context) != UDTs[t].end())
            return UDTs[t][context];
    auto ty = UDTs[t][context] = arena.Allocate<UserDefinedType>(t, *this, context);
    return ty;
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

#pragma warning(disable : 4800)
bool Semantic::IsLvalueType(Type* t) {
    return dynamic_cast<LvalueType*>(t);
}
bool Semantic::IsRvalueType(Type* t) {
    return dynamic_cast<RvalueType*>(t);
}
#pragma warning(default : 4800)
OverloadSet* Analyzer::GetOverloadSet(OverloadSet* f, OverloadSet* s) {
    if (CombinedOverloadSets[f].find(s) != CombinedOverloadSets[f].end())
        return CombinedOverloadSets[f][s];
    return CombinedOverloadSets[f][s] = arena.Allocate<OverloadSet>(f, s);
}
FloatType* Analyzer::GetFloatType(unsigned bits) {
    if (FloatTypes.find(bits) != FloatTypes.end())
        return FloatTypes[bits];
    return FloatTypes[bits] = arena.Allocate<FloatType>(bits);
}
Wide::Util::optional<Expression> Analyzer::LookupIdentifier(Type* context, const AST::Identifier* ident) {
    if (context == nullptr)
        return Wide::Util::none;
    Context c(*this, ident->location, [](ConcreteExpression e) {
        assert(false);
    });
    context = context->Decay();
    if (auto fun = dynamic_cast<Function*>(context)) {
        auto lookup = fun->LookupLocal(ident->val, c);
        if (lookup)
            return lookup;
        if (auto udt = dynamic_cast<UserDefinedType*>(fun->GetContext(*this)->Decay())) {
            auto self = fun->LookupLocal("this", c);
            lookup = self->AccessMember(ident->val, c);
            if (!lookup)
                return LookupIdentifier(udt->GetContext(*this), ident);
            return lookup;
        }
        return LookupIdentifier(fun->GetContext(*this), ident);
    }
    if (auto mod = dynamic_cast<Module*>(context)) {
        auto lookup = mod->AccessMember(mod->BuildValueConstruction(c), ident->val, c);
        if (!lookup)
            return LookupIdentifier(mod->GetContext(*this), ident);
        if (!dynamic_cast<OverloadSet*>(lookup->Resolve(nullptr).t))
            return lookup;
        auto lookup2 = LookupIdentifier(mod->GetContext(*this), ident);
        if (!lookup2)
            return lookup;
        if (dynamic_cast<OverloadSet*>(lookup2->Resolve(nullptr).t))
            return GetOverloadSet(dynamic_cast<OverloadSet*>(lookup->Resolve(nullptr).t), dynamic_cast<OverloadSet*>(lookup2->Resolve(nullptr).t))->BuildValueConstruction(c);
        return lookup;
    }
    if (auto udt = dynamic_cast<UserDefinedType*>(context)) {
        return LookupIdentifier(context->GetContext(*this), ident);
    }
    __debugbreak();
    auto value = context->AccessMember(context->BuildValueConstruction(c), ident->val, c);
    if (value)
        return value;
    return LookupIdentifier(context->GetContext(*this), ident);
}
Module* Analyzer::GetGlobalModule() {
    return global;
}
OverloadSet* Analyzer::GetOverloadSet(std::unordered_set<clang::NamedDecl*> decls, ClangUtil::ClangTU* from, Type* context) {
    if (clang_overload_sets.find(decls) != clang_overload_sets.end())
        if (clang_overload_sets[decls].find(context) != clang_overload_sets[decls].end())
            return clang_overload_sets[decls][context];
    return clang_overload_sets[decls][context] = arena.Allocate<OverloadSet>(std::move(decls), from, context);
}