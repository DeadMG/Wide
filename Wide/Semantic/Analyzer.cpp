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
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/LambdaType.h>
#include <Wide/Util/DebugUtilities.h>
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
        ConcreteExpression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) override {
            return val;
        }
    };
    null = arena.Allocate<NullType>();
    NothingFunctor = arena.Allocate<NothingCall>();
    struct decltypetype : public MetaType { 
        ConcreteExpression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c) override {
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
        ConcreteExpression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override {
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
        ConcreteExpression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override {
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
    EmptyOverloadSet = arena.Allocate<OverloadSet>(std::unordered_set<OverloadResolvable*>(), nullptr);
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

ConcreteExpression Analyzer::AnalyzeExpression(Type* t, const AST::Expression* e, std::function<void(ConcreteExpression)> handler) {
    if (auto semexpr = dynamic_cast<const SemanticExpression*>(e)) {
        return semexpr->e;
    }

    struct AnalyzerVisitor : public AST::Visitor<AnalyzerVisitor> {
        Type* t;
        Analyzer* self;
        Wide::Util::optional<ConcreteExpression> out;
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
            std::vector<ConcreteExpression> destructors;
            auto fun = self->AnalyzeExpression(t, funccall->callee, handler);
            std::vector<ConcreteExpression> args;
            for(auto&& arg : funccall->args) {
                args.push_back(self->AnalyzeExpression(t, arg, [&](ConcreteExpression e) { destructors.push_back(e); }));
            }
            
            out = fun.BuildCall(std::move(args), std::move(destructors), Context(*self, funccall->location, handler));
        }
        void VisitIdentifier(const AST::Identifier* ident) {
            auto mem = self->LookupIdentifier(t, ident);
            if (!mem) throw std::runtime_error("Attempted to access a member that did not exist.");
            out = *mem;
        }
        void VisitBinaryExpression(const AST::BinaryExpression* bin) {
            std::vector<ConcreteExpression> destructors;
            auto lhs = self->AnalyzeExpression(t, bin->lhs, handler);
            auto rhs = self->AnalyzeExpression(t, bin->rhs, [&](ConcreteExpression e) { destructors.push_back(e); });
            out = lhs.BuildBinaryExpression(rhs, bin->type, std::move(destructors), Context(*self, bin->location, handler));
        }
        void VisitMetaCall(const AST::MetaCall* mcall) {
            auto fun = self->AnalyzeExpression(t, mcall->callee, handler);
            std::vector<ConcreteExpression> args;
            for(auto&& arg : mcall->args)
                args.push_back(self->AnalyzeExpression(t, arg, handler));
            
            out = fun.BuildMetaCall(std::move(args), Context(*self, mcall->location, handler));
        }
        void VisitInteger(const AST::Integer* integer) {
            out = ConcreteExpression( self->GetIntegralType(64, true), self->gen->CreateIntegralExpression(std::stoll(integer->integral_value), true, self->GetIntegralType(64, true)->GetLLVMType(*self)));
        }
        void VisitThis(const AST::This* loc) {
            auto fun = dynamic_cast<Function*>(t);
            if (fun) {
                auto mem = fun->LookupLocal("this", Context(*self, loc->location, handler));
                if (!mem) throw std::runtime_error("Attempted to access \"this\", but it was not found, probably because you were not in a member function.");
                out = *mem;
            } else
                out = self->LookupIdentifier(t, self->arena.Allocate<Wide::AST::Identifier>("this", loc->location));
        }
        void VisitNegate(const AST::Negate* ne) {
            out = self->AnalyzeExpression(t, ne->ex, handler).BuildNegate(Context(*self, ne->location, handler));
        }
        void VisitTuple(const AST::Tuple* tup) {
            std::vector<ConcreteExpression> exprs;
            for (auto expr : tup->expressions)
                exprs.push_back(self->AnalyzeExpression(t, expr, handler));
            std::vector<Type*> types;
            for (auto expr : exprs)
                types.push_back(expr.t->Decay());
            out = self->GetTupleType(types)->ConstructFromLiteral(exprs, Context(*self, tup->location, handler));
        }
        // Ugly to perform an AST-level transformation in the analyzer
        // But hey- the AST exists to represent the exact source.
        void VisitLambda(const AST::Lambda* lam) {
            // Need to not-capture things that would be available anyway.
            
            std::vector<std::unordered_set<std::string>> lambda_locals;
            // Only implicit captures.
            std::unordered_set<std::string> captures;
            struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
                std::vector<std::unordered_set<std::string>>* lambda_locals;
                std::unordered_set<std::string>* captures;
                void VisitVariableStatement(const AST::Variable* v) {
                    for(auto&& name : v->name)
                        lambda_locals->back().insert(name);
                }
                void VisitLambdaCapture(const AST::Variable* v) {
                    for (auto&& name : v->name)
                        lambda_locals->back().insert(name);
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
            {
                auto caps = std::move(captures);
                for (auto&& name : caps) {
                    if (auto fun = dynamic_cast<Function*>(t)) {
                        if (fun->HasLocalVariable(name))
                            captures.insert(name);
                        if (auto udt = dynamic_cast<UserDefinedType*>(fun->GetContext(*self))) {
                            if (udt->HasMember(name))
                                captures.insert(name);
                        }
                    }
                }
            }
            
            // Just as a double-check, eliminate all explicit captures from the list. This should never have any effect
            // but I'll hunt down any bugs caused by eliminating it later.
            for(auto&& arg : lam->Captures)
                for(auto&& name : arg->name)
                    captures.erase(name);

            std::vector<std::pair<std::string, ConcreteExpression>> cap_expressions;
            for(auto&& arg : lam->Captures) {
                cap_expressions.push_back(std::make_pair(arg->name.front(), self->AnalyzeExpression(t, arg->initializer, handler)));
            }
            for(auto&& name : captures) {                
                AST::Identifier ident(name, lam->location);
                cap_expressions.push_back(std::make_pair(name, self->AnalyzeExpression(t, &ident, handler)));
            }
            std::vector<std::pair<std::string, Type*>> types;
            std::vector<ConcreteExpression> expressions;
            for (auto cap : cap_expressions) {
                types.push_back(std::make_pair(cap.first, cap.second.t->Decay()));
                expressions.push_back(cap.second);
            }
            auto type = self->arena.Allocate<LambdaType>(types, lam, *self);
            out = type->BuildLambdaFromCaptures(expressions, Context(*self, lam->location, handler));
        }
        void VisitDereference(const AST::Dereference* deref) {
            out = self->AnalyzeExpression(t, deref->ex, handler).BuildDereference(Context(*self, deref->location, handler));
        }
        void VisitIncrement(const AST::Increment* inc) {
            auto lhs = self->AnalyzeExpression(t, inc->ex, handler);
            out = lhs.BuildIncrement(inc->postfix, Context(*self, inc->location, handler));
        }
        void VisitType(const AST::Type* ty) {
            auto udt = self->GetUDT(ty, t->GetConstantContext(*self) ? t->GetConstantContext(*self) : t);
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

Function* Analyzer::GetWideFunction(const AST::FunctionBase* p, Type* context, const std::vector<Type*>& types) {
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
    // if (auto lval = dynamic_cast<LvalueType*>(t)) {
    //     return LvalueTypes[t] = lval;
    // }
    
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
    std::unordered_set<OverloadResolvable*> resolvable;
    for (auto x : set->functions)
        resolvable.insert(GetCallableForFunction(x, t));
    return GetOverloadSet(resolvable, t);
}
OverloadSet* Analyzer::GetOverloadSet() {
    return EmptyOverloadSet;
}
OverloadSet* Analyzer::GetOverloadSet(OverloadResolvable* c) {
    std::unordered_set<OverloadResolvable*> set;
    set.insert(c);
    return GetOverloadSet(set);
}
OverloadSet* Analyzer::GetOverloadSet(std::unordered_set<OverloadResolvable*> set, Type* nonstatic) {
    if (callable_overload_sets.find(set) != callable_overload_sets.end())
        return callable_overload_sets[set];
    if (nonstatic && (dynamic_cast<UserDefinedType*>(nonstatic->Decay()) || dynamic_cast<ClangType*>(nonstatic->Decay())))
        return callable_overload_sets[set] = arena.Allocate<OverloadSet>(set, nonstatic);
    return callable_overload_sets[set] = arena.Allocate<OverloadSet>(set, nullptr);
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
TupleType* Analyzer::GetTupleType(std::vector<Type*> types) {
    if (tupletypes.find(types) != tupletypes.end())
        return tupletypes[types];
    return tupletypes[types] = arena.Allocate<TupleType>(types, *this);
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
Wide::Util::optional<ConcreteExpression> Analyzer::LookupIdentifier(Type* context, const AST::Identifier* ident) {
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
        if (auto lam = dynamic_cast<LambdaType*>(fun->GetContext(*this)->Decay())) {
            auto self = fun->LookupLocal("this", c);
            lookup = lam->LookupCapture(*self, ident->val, c);
            if (!lookup)
                return LookupIdentifier(lam->GetContext(*this), ident);
            return lookup;
        }
        return LookupIdentifier(fun->GetContext(*this), ident);
    }
    if (auto mod = dynamic_cast<Module*>(context)) {
        auto lookup = mod->AccessMember(mod->BuildValueConstruction(c), ident->val, c);
        if (!lookup)
            return LookupIdentifier(mod->GetContext(*this), ident);
        if (!dynamic_cast<OverloadSet*>(lookup->t))
            return lookup;
        auto lookup2 = LookupIdentifier(mod->GetContext(*this), ident);
        if (!lookup2)
            return lookup;
        if (dynamic_cast<OverloadSet*>(lookup2->t))
            return GetOverloadSet(dynamic_cast<OverloadSet*>(lookup->t), dynamic_cast<OverloadSet*>(lookup2->t))->BuildValueConstruction(c);
        return lookup;
    }
    if (auto udt = dynamic_cast<UserDefinedType*>(context)) {
        return LookupIdentifier(context->GetContext(*this), ident);
    }
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
OverloadResolvable* Analyzer::GetCallableForFunction(const AST::FunctionBase* f, Type* context) {
    if (FunctionCallables.find(f) != FunctionCallables.end())
        return FunctionCallables.at(f);

    struct FunctionCallable : public OverloadResolvable {
        FunctionCallable(const AST::FunctionBase* f, Type* con)
            : func(f), context(con) {}
        const AST::FunctionBase* func;
        Type* context;

        bool HasImplicitThis() {
            // If we are a member without an explicit this, then we have an implicit this.
            if (!dynamic_cast<UserDefinedType*>(context->Decay()) && !dynamic_cast<LambdaType*>(context->Decay()))
                return false;
            if (func->args.size() > 0) {
                if (func->args[0].name == "this") {
                    return false;
                }
            }
            return true;
        }
        
        unsigned GetArgumentCount() override final {
            if (HasImplicitThis())
                return func->args.size() + 1;
            return func->args.size();
        }

        Type* MatchParameter(Type* argument, unsigned num, Analyzer& a) override final {
            assert(num <= GetArgumentCount());

            // If we are a member and we have an explicit this then treat the first normally.
            // Else if we are a member, blindly accept whatever is given for argument 0 as long as it's the member type.
            // Else, treat the first argument normally.
            
            if (HasImplicitThis()) {
                if (num == 0) {
                    if (argument->Decay() == context->Decay()) {
                        return argument;
                    }
                    return nullptr;
                }
                --num;
            }

            struct OverloadSetLookupContext : public MetaType {
                Type* context;
                Type* member;
                Type* argument;
                Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override final {
                    if (name == "this") {
                        if (member)
                            return c->GetConstructorType(member->Decay())->BuildValueConstruction(c);
                        throw std::runtime_error("Attempt to access this in a non-member.");
                    }
                    if (name == "auto")
                        return c->GetConstructorType(argument->Decay())->BuildValueConstruction(c);
                    return Wide::Util::none;
                }
                Type* GetContext(Analyzer& a) override final {
                    return context;
                }
            };

            OverloadSetLookupContext lc;
            lc.argument = argument;
            lc.context = context;
            lc.member = dynamic_cast<UserDefinedType*>(context->Decay());
            
            auto parameter_type = func->args[num].type ? 
                dynamic_cast<ConstructorType*>(a.AnalyzeExpression(&lc, func->args[num].type, [](ConcreteExpression e) {}).t->Decay()) :
                a.GetConstructorType(argument->Decay());

            if (!parameter_type)
                throw Wide::Semantic::SemanticError(func->args[num].location, Wide::Semantic::Error::ExpressionNoType);

            if (argument->IsA(parameter_type->GetConstructedType(), a))
                return parameter_type->GetConstructedType();
            return nullptr;
        }

        Callable* GetCallableForResolution(std::vector<Type*> types, Analyzer& a) override final {
            return a.GetWideFunction(func, context, std::move(types));
        }
    };
    return FunctionCallables[f] = arena.Allocate<FunctionCallable>(f, context);
}