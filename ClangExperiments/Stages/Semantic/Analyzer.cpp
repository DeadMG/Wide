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
    GetWideModule(GlobalModule)->AddSpecialMember("cpp", Expression(arena.Allocate<ClangIncludeEntity>(*this), nullptr));
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

Expression Analyzer::AnalyzeExpression(Type* t, AST::Expression* e) {
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

    throw std::runtime_error("Unrecognized AST node");
}

ClangUtil::ClangTU* Analyzer::LoadCPPHeader(std::string file) {
    if (headers.find(file) != headers.end())
        return &headers.find(file)->second;
    auto lam = [&](std::string err) {
        std::cout << err;
    };
   // if (gen)
        headers.insert(std::make_pair(file, ClangUtil::ClangTU(gen->context, file, ccs)));
   // else
   //     headers.insert(std::make_pair(file, ClangUtil::ClangTU(gen->context, file, ccs)));
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

Function* Analyzer::GetWideFunction(AST::Function* p) {
    if (WideFunctions.find(p) != WideFunctions.end())
        return WideFunctions[p];
    return WideFunctions[p] = arena.Allocate<Function>(std::vector<Type*>(), p, *this);
}

Module* Analyzer::GetWideModule(AST::Module* p) {
    if (WideModules.find(p) != WideModules.end())
        return WideModules[p];
    return WideModules[p] = arena.Allocate<Module>(p);
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

RvalueType* Analyzer::GetRvalueType(Type* t) {    
    if (t == Void)
        throw std::runtime_error("Can't get an rvalue ref to void.");

    if (RvalueTypes.find(t) != RvalueTypes.end())
        return RvalueTypes[t];

    // Prefer hash lookup to dynamic_cast.
    if (auto rval = dynamic_cast<RvalueType*>(t)) {
        return RvalueTypes[t] = rval;
    }

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

OverloadSet* Analyzer::GetOverloadSet(AST::FunctionOverloadSet* set) {
    if (OverloadSets.find(set) != OverloadSets.end())
        return OverloadSets[set];
    return OverloadSets[set] = arena.Allocate<OverloadSet>(set, *this);
}
void Analyzer::AddClangType(clang::QualType t, Type* match) {
    if (ClangTypes.find(t) != ClangTypes.end())
        throw std::runtime_error("Attempt to AddClangType on a type that already had a Clang type.");
    ClangTypes[t] = match;
}