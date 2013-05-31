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
#include "PointerType.h"
#include "Bool.h"
#include "ClangTemplateClass.h"
#include "OverloadSet.h"
#include "UserDefinedType.h"
#include "NullType.h"
#include <sstream>
#include <iostream>
#include <unordered_set>

#pragma warning(push, 0)

#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace Wide {
    namespace ClangUtil {
        clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple); 
    }
}

struct SemanticExpression : public AST::Expression {
    Semantic::Expression e;
    SemanticExpression(Semantic::Expression expr)
        : e(expr), AST::Expression(Lexer::Range()) {}
};

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
    , null(nullptr)
{
    LiteralStringType = arena.Allocate<StringType>();
}

void Analyzer::operator()(AST::Module* GlobalModule) {
    struct decltypetype : public Type {  
        std::size_t size(Analyzer& a) { return llvm::DataLayout(a.gen->main.getDataLayout()).getTypeAllocSize(llvm::IntegerType::getInt8Ty(a.gen->context)); }
        std::size_t alignment(Analyzer& a) { return llvm::DataLayout(a.gen->main.getDataLayout()).getABIIntegerTypeAlignment(8); }
        std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) {
            std::stringstream typenam;
            typenam << this;
            auto nam = typenam.str();
            return [=](llvm::Module* mod) -> llvm::Type* {
                if (mod->getTypeByName(nam))
                    return mod->getTypeByName(nam);
                return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
            };
        }

        Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
            if (args.size() > 1)
                throw std::runtime_error("Attempt to construct a type object with too many arguments.");
            if (args.size() == 1 && args[0].t->Decay() != this)
                throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
            return mem;
        }

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

    GetWideModule(GlobalModule)->AddSpecialMember("byte", GetWideModule(GlobalModule)->AccessMember(Expression(), "uint8", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("int", GetWideModule(GlobalModule)->AccessMember(Expression(), "int32", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("short", GetWideModule(GlobalModule)->AccessMember(Expression(), "int16", *this));
    GetWideModule(GlobalModule)->AddSpecialMember("long", GetWideModule(GlobalModule)->AccessMember(Expression(), "int64", *this));

    GetWideModule(GlobalModule)->AddSpecialMember("null", GetNullType()->BuildValueConstruction(std::vector<Expression>(), *this));

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
        auto fun = AnalyzeExpression(t, funccall->callee);
        std::vector<Expression> args;
        for(auto&& arg : funccall->args) {
            args.push_back(AnalyzeExpression(t, arg));
            if (!args.back().Expr)
                __debugbreak();
        }

        return fun.t->BuildCall(fun, std::move(args), *this);
    }

    if (auto ident = dynamic_cast<AST::IdentifierExpr*>(e)) {
        while (auto udt = dynamic_cast<UserDefinedType*>(t))
            t = GetDeclContext(udt->GetDeclContext()->higher);
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
        return Expression( GetIntegralType(64, true), gen->CreateIntegralExpression(std::stoll(integer->integral_value), true, GetIntegralType(64, true)->GetLLVMType(*this)));
    }
    if (dynamic_cast<AST::ThisExpression*>(e)) {
        return t->AccessMember(Expression(), "this", *this);
    }

    if (auto ge = dynamic_cast<AST::LTExpression*>(e)) {
        auto lhs = AnalyzeExpression(t, ge->lhs);
        auto rhs = AnalyzeExpression(t, ge->rhs);
        return lhs.t->BuildLTComparison(lhs, rhs, *this);
    }
    if (auto ne = dynamic_cast<AST::NegateExpression*>(e)) {
        auto expr = AnalyzeExpression(t, ne->ex);
        expr.Expr = gen->CreateNegateExpression(expr.t->BuildBooleanConversion(expr, *this));
        expr.t = Boolean;
        return expr;
    }

    // Ugly to perform an AST-level transformation in the analyzer
    // But hey- the AST exists to represent the exact source.
    if (auto lam = dynamic_cast<AST::Lambda*>(e)) {
        auto context = t->GetDeclContext()->higher;
        while(auto udt = dynamic_cast<AST::Type*>(context))
            context = udt->higher;
        auto ty = arena.Allocate<AST::Type>(context, "");
        auto ovr = arena.Allocate<AST::FunctionOverloadSet>("()", ty);
        auto fargs = lam->args;
        auto fun = arena.Allocate<AST::Function>("()", lam->statements, std::vector<AST::Statement*>(), lam->location, std::move(fargs), ty, std::vector<AST::VariableStatement*>());
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
            auto init = AnalyzeExpression(t, arg->initializer);
            AST::FunctionArgument f;
            f.name = str.str();
            f.type = arena.Allocate<SemanticExpression>(Expression(GetConstructorType(init.t), nullptr));
            ty->variables.push_back(arena.Allocate<AST::VariableStatement>(arg->name, arena.Allocate<SemanticExpression>(Expression(GetConstructorType(init.t->Decay()), nullptr)), Lexer::Range()));
            initializers.push_back(arena.Allocate<AST::VariableStatement>(arg->name, arena.Allocate<AST::IdentifierExpr>(str.str(), Lexer::Range()), Lexer::Range()));
            funargs.push_back(f);
            ++num;
            args.push_back(init);
        }
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
            ty->variables.push_back(arena.Allocate<AST::VariableStatement>(name, arena.Allocate<SemanticExpression>(Expression(GetConstructorType(capty), nullptr)), Lexer::Range()));
            ++num;
            args.push_back(t->AccessMember(Expression(), name, *this));        
        }
        auto conoverset = arena.Allocate<AST::FunctionOverloadSet>("type", ty);
        conoverset->functions.push_back(arena.Allocate<AST::Function>("type", std::vector<AST::Statement*>(), std::vector<AST::Statement*>(), Lexer::Range(), std::move(funargs), ty, std::move(initializers)));
        ty->Functions["type"] = conoverset;
        auto lamty = GetUDT(ty, t);
        AddCopyConstructor(ty, lamty);
        AddMoveConstructor(ty, lamty);
        auto obj = lamty->BuildRvalueConstruction(std::move(args), *this);
        return obj;
    }

    if (auto semexpr = dynamic_cast<SemanticExpression*>(e)) {
        return semexpr->e;
    }

    if (auto derefexpr = dynamic_cast<AST::DereferenceExpression*>(e)) {
        auto obj = AnalyzeExpression(t, derefexpr->ex);
        return obj.t->BuildDereference(obj, *this);
    }

    if (auto orexpr = dynamic_cast<AST::OrExpression*>(e)) {
        auto lhs = AnalyzeExpression(t, orexpr->lhs);
        auto rhs = AnalyzeExpression(t, orexpr->rhs);
        return lhs.t->BuildOr(lhs, rhs, *this);
    }

    if (auto andexpr = dynamic_cast<AST::AndExpression*>(e)) {
        auto lhs = AnalyzeExpression(t, andexpr->lhs);
        auto rhs = AnalyzeExpression(t, andexpr->rhs);
        return lhs.t->BuildAnd(lhs, rhs, *this);
    }

    if (auto mul = dynamic_cast<AST::Multiply*>(e)) {
        auto lhs = AnalyzeExpression(t, mul->lhs);
        auto rhs = AnalyzeExpression(t, mul->rhs);
        return lhs.t->BuildMultiply(lhs, rhs, *this);
    }
    if (auto inc = dynamic_cast<AST::Increment*>(e)) {
        auto lhs = AnalyzeExpression(t, inc->ex);
        return lhs.t->BuildIncrement(lhs, inc->postfix, *this);
    }
    if (auto plus = dynamic_cast<AST::Addition*>(e)) {
        auto lhs = AnalyzeExpression(t, plus->lhs);
        auto rhs = AnalyzeExpression(t, plus->rhs);
        return lhs.t->BuildPlus(lhs, rhs, *this);
    }

    if (auto ty = dynamic_cast<AST::Type*>(e)) {
        ty->higher = t->GetDeclContext();
        auto udt = GetUDT(ty, t);
        return GetConstructorType(udt)->BuildValueConstruction(std::vector<Expression>(), *this);
    }

    if (auto ptr = dynamic_cast<AST::PointerAccess*>(e)) {
        auto expr = AnalyzeExpression(t, ptr->ex);
        return expr.t->PointerAccessMember(expr, ptr->member, *this);
    }

    if (auto add = dynamic_cast<AST::AddressOfExpression*>(e)) {
        auto expr = AnalyzeExpression(t, add->ex);
        return expr.t->AddressOf(expr, *this);
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
    throw std::runtime_error("Encountered a DeclContext that was not a module.");
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
        initializers.push_back(arena.Allocate<AST::VariableStatement>(m.name, arena.Allocate<SemanticExpression>(other.t->AccessMember(other, m.name, *this)), Lexer::Range()));
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
    for(auto&& m : members) {
        initializers.push_back(arena.Allocate<AST::VariableStatement>(m.name, nullptr, Lexer::Range()));
    }
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

NullType* Analyzer::GetNullType() {
    if (null) return null;
    return null = arena.Allocate<NullType>();
}