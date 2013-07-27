#include <Wide/Semantic/Function.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/Function.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>

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

Function::Function(std::vector<Type*> args, AST::Function* astfun, Analyzer& a, UserDefinedType* mem)
: analyzer(a)
, ReturnType(nullptr)
, fun(astfun)
, codefun(nullptr)
, member(mem) {
    // Only match the non-concrete arguments.
    variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the argument scope
    unsigned num = 0;
    unsigned metaargs = 0;
    if (mem && (astfun->args.size() < 1 || astfun->args.front().name != "this"))
        Args.push_back(a.GetLvalueType(mem));
    for(auto&& arg : astfun->args) {
        auto param = [this, num] { return num + ReturnType->IsComplexType() + (member != nullptr); };
        Type* ty = args[num];
        Expression var;
        var.t = a.GetLvalueType(ty);
        if (ty->IsComplexType()) {
            var.Expr = a.gen->CreateParameterExpression(param);
        } else {
            std::vector<Expression> args;
            Expression arg;
            arg.Expr = a.gen->CreateParameterExpression(param);
            arg.t = ty;
            args.push_back(arg);
            if (arg.t->IsReference())
                var.Expr = arg.Expr;
            else
                var.Expr = ty->BuildLvalueConstruction(args, a).Expr;
        }
        ++num;
        variables.back()[arg.name] = var;
        exprs.push_back(var.Expr);
        Args.push_back(ty);
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
        /*if (ident->val == "ReturnType") {
            auto ty = dynamic_cast<ConstructorType*>(expr.t);
            if (!ty)
                throw std::runtime_error("Prolog right-hand-side of ReturnType must be a type.");
            ReturnType = ty->GetConstructedType();
        }*/
    }
    if (ReturnType) {
        // We have the name and signature- declare, and generate the body later.
        codefun = a.gen->CreateFunction(GetLLVMType(a), name, this);
    }
}

AST::DeclContext* Function::GetDeclContext() {
    return fun;
}

clang::QualType Function::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    return GetSignature(a)->GetClangType(where, a);
}

std::function<llvm::Type*(llvm::Module*)> Function::GetLLVMType(Analyzer& a) {    
    return GetSignature(a)->GetLLVMType(a);
}
void Function::ComputeBody(Analyzer& a) {    
    if (!ReturnType) {        
        // Initializers first, if we are a constructor
        if (member && fun->name == "type") {
            auto self = AccessMember(Expression(), "this", a);
            auto members = member->GetMembers();
            for(auto&& x : members) {
                auto has_initializer = [&](std::string name) -> AST::VariableStatement* {
                    for(auto&& x : fun->initializers)
                        if(x->name == name)
                            return x;
                    return nullptr;
                };
                if (auto init = has_initializer(x.name)) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                    std::vector<Expression> args;
                    if (init->initializer)
                        args.push_back(a.AnalyzeExpression(this, init->initializer));
                    exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), a));
                } else {
                    // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                    if (x.InClassInitializer)
                    {
                        auto expr = a.AnalyzeExpression(this, x.InClassInitializer);
                        auto mem = self.t->AccessMember(self, x.name, a);                   
                        std::vector<Expression> args;
                        args.push_back(expr);
                        exprs.push_back(x.t->BuildInplaceConstruction(mem.Expr, std::move(args), a));
                        continue;
                    }
                    if (x.t->IsReference())
                        throw std::runtime_error("Failed to initialize a reference member.");
                    auto mem = self.t->AccessMember(self, x.name, a);                   
                    std::vector<Expression> args;
                    exprs.push_back(x.t->BuildInplaceConstruction(mem.Expr, std::move(args), a));
                }
            }
        }
        // Now the body.
        variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the function body scope.
        std::function<Codegen::Statement*(AST::Statement*)> AnalyzeStatement;
        AnalyzeStatement = [&](AST::Statement* stmt) -> Codegen::Statement* {        
            if (!stmt)
                return nullptr;
        
            if (auto e = dynamic_cast<AST::Expression*>(stmt))
                return analyzer.AnalyzeExpression(this, e).Expr;
        
            if (auto ret = dynamic_cast<AST::Return*>(stmt)) {
                if (!ret->RetExpr) {
                    ReturnType = a.GetVoidType();              
                    return a.gen->CreateReturn();
                } else {
                    // When returning an lvalue or rvalue, decay them.
                    auto result = a.AnalyzeExpression(this, ret->RetExpr);
                    if (!ReturnType)
                        ReturnType = result.t->Decay();
                    else if (ReturnType != result.t->Decay() && (a.RankConversion(result.t, ReturnType) == ConversionRank::None || a.RankConversion(ReturnType, result.t->Decay()) != ConversionRank::None))
                        throw std::runtime_error("Attempted to return more than 1 post-decay type from a function.");
                    
                    // Deal with emplacing the result
                    if (ReturnType->IsComplexType()) {
                        std::vector<Expression> args;
                        args.push_back(result);
                        return a.gen->CreateReturn(ReturnType->BuildInplaceConstruction(a.gen->CreateParameterExpression(0), std::move(args), a));
                    } else
                        return a.gen->CreateReturn(result.t->BuildValue(result, a).Expr);
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
                // This only works for AST function calls, e.g. f(), it doesn't work for, say, lambda expressions.
                // Go down to the LLVM level.
                /*if (auto func = dynamic_cast<AST::FunctionCallExpr*>(var->initializer)) {
                    if (result.t->IsReference() && result.t->IsReference()->IsComplexType()) {
                        variables.back()[var->name].t = a.GetLvalueType(result.t);
                        return variables.back()[var->name].Expr = result.Expr;
                    }
                }*/
                std::vector<Expression> args;
                args.push_back(result);
        
                if (result.t->IsReference()) {
                    if (!result.steal) {
                        result.t = a.GetLvalueType(result.t);
                        variables.back()[var->name] = result.t->IsReference()->BuildLvalueConstruction(args, a);         
                    } else {
                        result.steal = false;
                        // If I've been asked to steal an lvalue, it's because I rvalue constructed an lvalue reference
                        // So the expr will be T** whereas I want just T*
                        //if (dynamic_cast<LvalueType*>(result.t))
                        //    result.Expr = a.gen->CreateLoad(result.Expr);
                        result.t = a.GetLvalueType(result.t);
                        variables.back()[var->name] = result;
                    }
                } else {
                    variables.back()[var->name] = result.t->BuildLvalueConstruction(args, a);
                }
                return variables.back()[var->name].Expr;
            }
        
            if (auto comp = dynamic_cast<AST::CompoundStatement*>(stmt)) {
                variables.push_back(std::unordered_map<std::string, Expression>());
                if (comp->stmts.size() == 0)
                    return nullptr;
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
        
        // Prevent infinite recursion by setting this variable as soon as the return type is ready
        // Else GetLLVMType() will call us again to prepare the return type.

        // Occurs if no return statements.
        if (!ReturnType) { ReturnType = a.GetVoidType(); exprs.push_back(a.gen->CreateReturn()); }
        if (ReturnType == a.GetVoidType() && !dynamic_cast<Codegen::ReturnStatement*>(exprs.back()))
            exprs.push_back(a.gen->CreateReturn());
        if (!codefun)
            codefun = a.gen->CreateFunction(GetLLVMType(a), name, this);
        for(auto&& x : exprs)
            codefun->AddStatement(x);
        variables.clear();
    }
}
Expression Function::BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
    Expression val;
    val.t = this;
    val.Expr = a.gen->CreateFunctionValue(name);
    return GetSignature(a)->BuildCall(val, std::move(args), a);
}
Expression Function::AccessMember(Expression e, std::string name, Analyzer& a) {
    for(auto it = variables.rbegin(); it != variables.rend(); ++it) {
        auto&& map = *it;
        if (map.find(name) != map.end()) {
            return map[name];
        }
    }
    if (member) {
        Expression self(a.GetLvalueType(member), a.gen->CreateParameterExpression([this] { return ReturnType->IsComplexType(); }));
        if (name == "this")
            return self;
        if (member->HasMember(name))
            return self.t->AccessMember(self, std::move(name), a);
        auto context = member->GetDeclContext()->higher;
        while(!dynamic_cast<AST::Module*>(context))
            context = context->higher;
        return a.GetDeclContext(context)->AccessMember(Expression(), name, a);
    }
    return a.GetDeclContext(fun->higher)->AccessMember(e, std::move(name), a);
}
std::string Function::GetName() {
    return name;
}
FunctionType* Function::GetSignature(Analyzer& a) {
    if (!ReturnType)
        ComputeBody(a);
    return a.GetFunctionType(ReturnType, Args);
}
bool Function::HasLocalVariable(std::string name) {
    for(auto it = variables.rbegin(); it != variables.rend(); ++it) {
        auto&& map = *it;
        if (map.find(name) != map.end()) {
            return true;
        }
    }
    return false;
}