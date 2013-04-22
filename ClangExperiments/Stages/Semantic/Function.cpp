#include "Function.h"
#include "../Parser/AST.h"
#include "FunctionType.h"
#include "Analyzer.h"
#include "../Codegen/Expression.h"
#include "../Codegen/Function.h"
#include "../Codegen/Generator.h"
#include "ClangTU.h"
#include "Module.h"
#include "LvalueType.h"
#include "ConstructorType.h"

#include <unordered_set>
#include <sstream>
#include <iostream>

#pragma warning(push, 0)

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/raw_os_ostream.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Function::Function(std::vector<Type*> args, AST::Function* astfun, Analyzer& a)
: analyzer(a)
, ReturnType(nullptr)
, fun(astfun)
, body(false)
, codefun(nullptr) {    
    // Deal with only typed parameters right now
    variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the argument scope
    unsigned num = 0;
    for(auto&& arg : astfun->args) {
        auto param = [this, num] { return ReturnType->IsComplexType() ? num + 1 : num; };
        auto ty = a.AnalyzeExpression(this, arg.type);
        auto con = dynamic_cast<ConstructorType*>(ty.t);
        if (!con)
            throw std::runtime_error("Attempted to create an argument, but the expression was not a type.");
        // If con's type is complex, expect ptr.
        auto t = con->GetConstructedType();
        Expression var;
        var.t = a.GetLvalueType(t);
        if (t->IsComplexType()) {
            var.Expr = a.gen->CreateParameterExpression(param);
        } else {
            std::vector<Expression> args;
            Expression arg;
            arg.Expr = a.gen->CreateParameterExpression(param);
            arg.t = t;
            args.push_back(arg);
            var.Expr = t->BuildLvalueConstruction(args, a).Expr;
        }
        ++num;
        variables.back()[arg.name] = var;
        Args.push_back(t);
    }

    std::stringstream strstr;
    strstr << "__" << std::hex << this;
    name = strstr.str();

    // Deal with the prolog first.
    for(auto&& prolog : astfun->prolog) {
        auto ass = dynamic_cast<AST::AssignmentExpr*>(prolog);
        if (!ass)            
            throw std::runtime_error("Prologs can only be composed of assignment expressions right now!");
        auto ident = dynamic_cast<AST::IdentifierExpr*>(ass->lhs);
        if (!ident)
            throw std::runtime_error("Prolog assignment expressions must have a plain identifier on the left-hand-side right now!");
        auto expr = a.AnalyzeExpression(this, ass->rhs);
        if (ident->val == "ExportName") {
            auto str = dynamic_cast<Codegen::StringExpression*>(expr.Expr);
            if (!str)
                throw std::runtime_error("Prolog right-hand-sides of ExportName must be of string type!");
            name = str->GetContents();     
        }
        if (ident->val == "ReturnType") {
            auto ty = dynamic_cast<ConstructorType*>(expr.t);
            if (!ty)
                throw std::runtime_error("Prolog right-hand-side of ReturnType must be a type.");
            ReturnType = ty->GetConstructedType();
        }
    }
    if (ReturnType) {
        // We have the name and signature- declare, and generate the body later.
        codefun = a.gen->CreateFunction(GetLLVMType(a), name);
    }
}

clang::QualType Function::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    if (!ReturnType)
        ComputeBody(a);
    return a.GetFunctionType(ReturnType, Args)->GetClangType(where, a);
}

std::function<llvm::Type*(llvm::Module*)> Function::GetLLVMType(Analyzer& a) {    
    return a.GetFunctionType(ReturnType, Args)->GetLLVMType(a);
}
void Function::ComputeBody(Analyzer& a) {    
    if (!body) {        
        // Now the body.
        std::vector<std::tuple<unsigned, Type*>> RetExpressions;
        std::unordered_set<Type*> RetTypes;
        variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the function body scope.
        std::function<Codegen::Statement*(AST::Statement*)> AnalyzeStatement;
        AnalyzeStatement = [&](AST::Statement* stmt) -> Codegen::Statement* {        
            if (!stmt)
                return nullptr;
        
            if (auto e = dynamic_cast<AST::Expression*>(stmt))
                return analyzer.AnalyzeExpression(this, e).Expr;
        
            if (auto ret = dynamic_cast<AST::Return*>(stmt)) {
                if (!ret->RetExpr) {
                    RetTypes.insert(a.Void);                
                    return a.gen->CreateReturn();
                } else {
                    // When returning an lvalue or rvalue, decay them.
                    auto result = a.AnalyzeExpression(this, ret->RetExpr);
                    if (result.t->IsReference() && !result.t->IsReference()->IsComplexType()) {
                        result = result.t->BuildValue(result, a);
                        RetTypes.insert(result.t);
					} else {
						if (result.t->IsReference())
							RetTypes.insert(result.t->IsReference());
						else
							RetTypes.insert(result.t);
					}
                    RetExpressions.push_back(std::make_tuple(exprs.size(), result.t));
                    return a.gen->CreateReturn(result.Expr);
                }
            }
            if (auto var = dynamic_cast<AST::VariableStatement*>(stmt)) {
                auto result = a.AnalyzeExpression(this, var->initializer);
                
                for(auto scope = variables.begin(); scope != variables.end(); ++scope) {
                    if (scope->find(var->name) != scope->end())
                        throw std::runtime_error("Error: variable shadowing " + var->name);
                }

                // If it's a function call, and it returns a complex T, then the return expression already points to
                // a memory variable of type T. Just use that.
                if (auto func = dynamic_cast<AST::FunctionCallExpr*>(var->initializer)) {
                    if (result.t->IsReference() && result.t->IsReference()->IsComplexType()) {
                        variables.back()[var->name].t = a.GetLvalueType(result.t);
                        return variables.back()[var->name].Expr = result.Expr;
                    }
                }

                std::vector<Expression> args;
                args.push_back(result);
        
                if (result.t->IsReference()) {
                    result.t = a.GetLvalueType(result.t);
                    variables.back()[var->name] = result.t->IsReference()->BuildLvalueConstruction(args, a);         
                } else {
                    variables.back()[var->name] = result.t->BuildLvalueConstruction(args, a);
                }
                return variables.back()[var->name].Expr;
            }
        
            if (auto comp = dynamic_cast<AST::CompoundStatement*>(stmt)) {
                variables.push_back(std::unordered_map<std::string, Expression>());
                if (comp->stmts.size() == 0)
                    return nullptr;
                if (comp->stmts.size() == 1)
                    return AnalyzeStatement(comp->stmts.back());
                Codegen::Statement* chain = nullptr;
                for(auto&& x : comp->stmts) {
                    if (!chain) chain = AnalyzeStatement(x);
                    else chain = a.gen->CreateChainStatement(chain, AnalyzeStatement(x));
                }
                variables.pop_back();
                return chain;
            }
        
            if (auto if_stmt = dynamic_cast<AST::IfStatement*>(stmt)) {
                auto cond = a.AnalyzeExpression(this, if_stmt->condition);
                auto expr = cond.t->BuildBooleanConversion(cond, a);
                return a.gen->CreateIfStatement(expr, AnalyzeStatement(if_stmt->true_statement), AnalyzeStatement(if_stmt->false_statement));
            }
            
            if (auto while_stmt = dynamic_cast<AST::WhileStatement*>(stmt)) {
                auto cond = a.AnalyzeExpression(this, while_stmt->condition);
                auto expr = cond.t->BuildBooleanConversion(cond, a);
                return a.gen->CreateWhile(expr, AnalyzeStatement(while_stmt->body));
            }
        
            throw std::runtime_error("Unsupported statement.");
        };
        for(std::size_t i = 0; i < fun->statements.size(); ++i) {
            exprs.push_back(AnalyzeStatement(fun->statements[i]));
        }
        
        if (RetTypes.empty()) {
            RetTypes.insert(ReturnType = a.Void);
            exprs.push_back(a.gen->CreateReturn());
        } else if (RetTypes.size() == 1) {
            ReturnType = *RetTypes.begin();
            if (ReturnType->IsComplexType()) {
                // If we're complex then first param is pointer to result, so fix up all returns to be an inplace construction to that expression.
                for(auto&& tup : RetExpressions) {
                    Type* ty;
                    unsigned num;
                    std::tie(num, ty) = tup;
                    std::vector<Expression> args;
                    Expression out;
                    out.Expr = static_cast<Codegen::ReturnStatement*>(exprs[num])->GetReturnExpression();
                    out.t = ty;
                    args.push_back(out);
                    exprs[num] = ReturnType->BuildInplaceConstruction(a.gen->CreateParameterExpression(0), args, a);
                    exprs.insert(exprs.begin() + num + 1, a.gen->CreateReturn());
                }
            }
        } else
            throw std::runtime_error("Attempted to return more than one type. Type deduction currently needs exact matches between return statements.");
        
        if (!codefun)
            codefun = a.gen->CreateFunction(GetLLVMType(a), name);
        for(auto&& x : exprs)
            codefun->AddStatement(x);
        body = true;
        variables.clear();
    }
}
Expression Function::BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
    // Expect e to be null.
    // We need the body to get the return type if it was not in the prolog, so lazy generate it.
    if (!body)
        ComputeBody(a);
    Expression val;
    val.t = this;
    val.Expr = a.gen->CreateFunctionValue(name);
    return a.GetFunctionType(ReturnType, Args)->BuildCall(val, std::move(args), a);
}
Expression Function::AccessMember(Expression e, std::string name, Analyzer& a) {
    for(auto it = variables.rbegin(); it != variables.rend(); ++it) {
        auto&& map = *it;
        if (map.find(name) != map.end()) {
            return map[name];
        }
    }
    return a.GetWideModule(fun->higher)->AccessMember(e, name, a);
}
std::string Function::GetName() {
    return name;
}
FunctionType* Function::GetSignature(Analyzer& a) {
    return a.GetFunctionType(ReturnType, Args);
}