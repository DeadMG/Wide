#include "Analyzer.h"
#include "Type.h"
#include "ClangTU.h"
#include "../Parser/AST.h"
#include "../Codegen/Expression.h"
#include "../Codegen/Generator.h"
#include "ClangType.h"
#include "StringType.h"
#include "Module.h"
#include "Function.h"
#include "LvalueType.h"
#include "FunctionType.h"
#include "ClangNamespace.h"
#include "Void.h"
#include "IntegralType.h"
#include "ConstructorType.h"
#include "RvalueType.h"
#include "ClangInclude.h"
#include "Bool.h"
#include "ClangTemplateClass.h"
#include "OverloadSet.h"
#include "UserDefinedType.h"
#include <sstream>
#include <iostream>
#include <unordered_set>

#pragma warning(push, 0)

#include <clang/AST/Type.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace Wide {
    namespace ClangUtil {
        clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple); 
    }
}

ClangCommonState::ClangCommonState(const Options::Clang& opts)
        : Options(&opts)
        , error_stream(errors)
        , FileManager(opts.FileSearchOptions)
        , engine(opts.DiagnosticIDs, opts.DiagnosticOptions.getPtr(), new clang::TextDiagnosticPrinter(error_stream, opts.DiagnosticOptions.getPtr()), false)
        , targetinfo(Wide::ClangUtil::CreateTargetInfoFromTriple(engine, opts.TargetOptions.Triple))
        , hs(opts.HeaderSearchOptions, FileManager, engine, opts.LanguageOptions, targetinfo.get())
        , layout(targetinfo->getTargetDescription())
{
}

// After definition of type
Analyzer::~Analyzer() {}

Analyzer::Analyzer(const Options::Clang& opts, Codegen::Generator* g)    
    : ccs(opts)
    , gen(g)
{
    LiteralStringType = arena.Allocate<StringType>();
}

void Analyzer::operator()(AST::Module* GlobalModule) {
    GetWideModule(GlobalModule)->AddSpecialMember("cpp", Expression(arena.Allocate<ClangIncludeEntity>(), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("void", Expression(GetConstructorType(Void = arena.Allocate<VoidType>()), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("global", Expression(GetWideModule(GlobalModule), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("int8", Expression(GetConstructorType(Int8 = arena.Allocate<IntegralType>(8)), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("bool", Expression(GetConstructorType(Boolean = arena.Allocate<Bool>()), nullptr));
    GetWideModule(GlobalModule)->AddSpecialMember("true", Expression(Boolean, gen->CreateInt8Expression(1)));
    GetWideModule(GlobalModule)->AddSpecialMember("false", Expression(Boolean, gen->CreateInt8Expression(0)));

    try {      
        GetWideModule(GlobalModule)->AccessMember(Expression(), "Standard", *this).t->AccessMember(Expression(), "Main", *this).t->BuildCall(Expression(), std::vector<Expression>(), *this);
        for(auto&& x : this->headers)
            x.second.GenerateCodeAndLinkModule(&gen->main);
    } catch(std::runtime_error& e) {
        std::cout << "Error!\n" << e.what();
        __debugbreak();
    }
}

#include "../Parser/ASTVisitor.h"

Expression Analyzer::AnalyzeExpression(Type* t, AST::Expression* e) {
    struct SemanticExpression : public AST::Expression {
        Semantic::Expression e;
        SemanticExpression(Semantic::Expression expr)
            : e(expr), AST::Expression(Lexer::Range()) {}
    };

    if (auto str = dynamic_cast<AST::StringExpr*>(e)) {
        Expression out;
        out.t = LiteralStringType;
        if (gen)
            out.Expr = gen->CreateStringExpression(str->val);
        return out;
    }

    if (auto shift = dynamic_cast<AST::LeftShiftExpr*>(e)) {
        auto lhs = AnalyzeExpression(t, shift->lhs);
        auto rhs = AnalyzeExpression(t, shift->rhs);

        return lhs.t->BuildLeftShift(lhs, rhs, *this);
    }

    if (auto shift = dynamic_cast<AST::RightShiftExpr*>(e)) {
        auto lhs = AnalyzeExpression(t, shift->lhs);
        auto rhs = AnalyzeExpression(t, shift->rhs);

        return lhs.t->BuildRightShift(lhs, rhs, *this);
    }

    if (auto access = dynamic_cast<AST::MemAccessExpr*>(e)) {
        auto val = AnalyzeExpression(t, access->expr);
        return val.t->AccessMember(val, access->mem, *this);
    }

    if (auto funccall = dynamic_cast<AST::FunctionCallExpr*>(e)) {
        // If we are of the form cpp(string), then perform Super Special Lols.
        // To avoid special casing CPP includes any more than necessary, permit calling non-objects.
        auto fun = AnalyzeExpression(t, funccall->callee);
        std::vector<Expression> args;
        for(auto&& arg : funccall->args)
            args.push_back(AnalyzeExpression(t, arg));

        return fun.t->BuildCall(fun, std::move(args), *this);
    }

    if (auto ident = dynamic_cast<AST::IdentifierExpr*>(e)) {
        while (auto udt = dynamic_cast<UserDefinedType*>(t))
            t = GetDeclContext(udt->GetDeclContext());
        return t->AccessMember(Expression(), ident->val, *this);
    }

    if (auto ass = dynamic_cast<AST::AssignmentExpr*>(e)) {
        auto lhs = AnalyzeExpression(t, ass->lhs);
        auto rhs = AnalyzeExpression(t, ass->rhs);
        return lhs.t->BuildAssignment(lhs, rhs, *this);
    }

    if (auto cmp = dynamic_cast<AST::EqCmpExpression*>(e)) {        
        auto lhs = AnalyzeExpression(t, cmp->lhs);
        auto rhs = AnalyzeExpression(t, cmp->rhs);
        return lhs.t->BuildEQComparison(lhs, rhs, *this);
    }

    if (auto mcall = dynamic_cast<AST::MetaCallExpr*>(e)) {
        auto fun = AnalyzeExpression(t, mcall->callee);
        std::vector<Expression> args;
        for(auto&& arg : mcall->args)
            args.push_back(AnalyzeExpression(t, arg));

        return fun.t->BuildMetaCall(fun, std::move(args), *this);
    }

    if (auto neq = dynamic_cast<AST::NotEqCmpExpression*>(e)) {     
        auto lhs = AnalyzeExpression(t, neq->lhs);
        auto rhs = AnalyzeExpression(t, neq->rhs);
        return lhs.t->BuildNEComparison(lhs, rhs, *this);        
    }

    if (auto ge = dynamic_cast<AST::GTExpression*>(e)) {
        auto lhs = AnalyzeExpression(t, ge->lhs);
        auto rhs = AnalyzeExpression(t, ge->rhs);
        return lhs.t->BuildGTComparison(lhs, rhs, *this);
    }

    if (auto integer = dynamic_cast<AST::IntegerExpression*>(e)) {
        return Expression( this->Int8, gen->CreateInt8Expression(std::stol(integer->integral_value)));
    }
    if (auto self = dynamic_cast<AST::ThisExpression*>(e)) {
        return t->AccessMember(Expression(), "this", *this);
    }

    if (auto ge = dynamic_cast<AST::LTExpression*>(e)) {
        auto lhs = AnalyzeExpression(t, ge->lhs);
        auto rhs = AnalyzeExpression(t, ge->rhs);
        return lhs.t->BuildLTComparison(lhs, rhs, *this);
    }

    // Ugly to perform an AST-level transformation in the analyzer
    // But hey- the AST exists to represent the exact source.
    if (auto lam = dynamic_cast<AST::Lambda*>(e)) {

        auto ty = arena.Allocate<AST::Type>(t->GetDeclContext(), "");
        auto ovr = arena.Allocate<AST::FunctionOverloadSet>("()", ty);
        auto fargs = lam->args;
        auto fun = arena.Allocate<AST::Function>("()", lam->statements, std::vector<AST::Statement*>(), lam->location, std::move(fargs), ty, std::vector<AST::VariableStatement*>());
        ovr->functions.push_back(fun);
        ty->Functions["()"] = ovr;

        // Need to not-capture things that would be available anyway.
        
        std::vector<std::unordered_set<std::string>> lambda_locals;
        std::unordered_set<std::string> captures;
        struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
            std::vector<std::unordered_set<std::string>>* lambda_locals;
            std::unordered_set<std::string>* captures;
            void VisitVariableStatement(AST::VariableStatement* v) {
                lambda_locals->back().insert(v->name);
            }
            void VisitLambdaArgument(AST::FunctionArgument* arg) {
                lambda_locals->back().insert(arg->name);
            }
            void VisitLambda(AST::Lambda* l) {
                lambda_locals->emplace_back();
                for(auto&& x : l->args)
                    VisitLambdaArgument(&x);
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
                    VisitStatement(cs);
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
        
        auto lamty = GetUDT(ty);
        
        // We obviously don't want to capture module-scope names.
        // Only capture from the local scope, and from "this".
        auto caps = std::move(captures);
        for(auto&& name : caps) {
            if (auto fun = dynamic_cast<Function*>(t)) {
                if (fun->HasLocalVariable(name))
                    captures.insert(name);
                auto context = GetDeclContext(fun->GetDeclContext());
                if (auto udt = dynamic_cast<UserDefinedType*>(context)) {
                    if (udt->HasMember(name))
                        captures.insert(name);
                }
            }
        }


        std::vector<AST::VariableStatement*> initializers;
        std::vector<AST::FunctionArgument> funargs;
        unsigned num = 0;
        for(auto&& name : captures) {
            std::stringstream str;
            str << "__param" << num;
            auto capty = t->AccessMember(Expression(), name, *this).t;
            initializers.push_back(arena.Allocate<AST::VariableStatement>(name, arena.Allocate<AST::IdentifierExpr>(str.str(), Lexer::Range()), Lexer::Range()));
            AST::FunctionArgument f;
            f.name = str.str();
            f.type = arena.Allocate<SemanticExpression>(Expression(GetConstructorType(capty), nullptr));
            funargs.push_back(f);
            if (!lam->defaultref)
                capty = capty->Decay();
            lamty->AddMemberVariable(capty, name);
            ++num;
        }
        std::vector<Expression> args;
        auto conoverset = arena.Allocate<AST::FunctionOverloadSet>("type", ty);
        conoverset->functions.push_back(arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(funargs), ty, std::move(initializers)));
        ty->Functions["type"] = conoverset;
        for(auto&& name : captures) {
            args.push_back(t->AccessMember(Expression(), name, *this));        
        }
        auto obj = lamty->BuildRvalueConstruction(std::move(args), *this);
        return obj;
    }

    if (auto semexpr = dynamic_cast<SemanticExpression*>(e)) {
        return semexpr->e;
    }

    throw std::runtime_error("Unrecognized AST node");
}

ClangUtil::ClangTU* Analyzer::LoadCPPHeader(std::string file) {
    if (headers.find(file) != headers.end())
        return &headers.find(file)->second;
    headers.insert(std::make_pair(file, ClangUtil::ClangTU(gen->context, file, ccs)));
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
    if (t->isCharType())
        return Int8;
    if (t->isVoidType())
        return Void;
    if (t->isPointerType()) {
        auto pt = t->getPointeeType();
        if (pt->isCharType())
            return LiteralStringType;
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
        return LvalueTypes[t] = GetLvalueType(rval->GetPointee());
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

OverloadSet* Analyzer::GetOverloadSet(AST::FunctionOverloadSet* set, UserDefinedType* t) {
    if (OverloadSets.find(set) != OverloadSets.end())
        return OverloadSets[set];
    return OverloadSets[set] = arena.Allocate<OverloadSet>(set, *this, t);
}
void Analyzer::AddClangType(clang::QualType t, Type* match) {
    if (ClangTypes.find(t) != ClangTypes.end())
        throw std::runtime_error("Attempt to AddClangType on a type that already had a Clang type.");
    ClangTypes[t] = match;
}

UserDefinedType* Analyzer::GetUDT(AST::Type* t) {
    if (UDTs.find(t) != UDTs.end())
        return UDTs[t];
    auto ty = UDTs[t] = arena.Allocate<UserDefinedType>(t, *this);
    DeclContexts[t] = ty;
    return ty;
}

Type* Analyzer::GetDeclContext(AST::DeclContext* con) {
    if (DeclContexts.find(con) != DeclContexts.end())
        return DeclContexts[con];
    if (auto mod = dynamic_cast<AST::Module*>(con))
        return DeclContexts[con] = GetWideModule(mod);
    if (auto ty = dynamic_cast<AST::Type*>(con))
        return DeclContexts[con] = GetUDT(ty);
    throw std::runtime_error("Encountered a DeclContext that was not a type or module.");
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
        if (rval->GetPointee() == from) {
            return ConversionRank::Zero;
        }
    }
    //          T T& T&& to
    //    T        x  
    //    T&    0     x
    //    T&&   0  x  
    //    U     2  x  2
    //    from

    // Since we just covered the only T& case, if T& then fail
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
    //          T            T&&
    //    T           
    //    T&    copyable    
    //    T&&   movable      
    //    U     convertible  U to T
    //    from

    // The only remaining cases are type-specific, not guaranteed for all types.
    // Ask "to" to handle it.
    return to->RankConversionFrom(from, *this);
}