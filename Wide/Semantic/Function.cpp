#include <Wide/Semantic/Function.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Reference.h>
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

Function::Function(std::vector<Type*> args, const AST::Function* astfun, Analyzer& a, Type* mem)
: analyzer(a)
, ReturnType(nullptr)
, fun(astfun)
, codefun(nullptr)
, context(mem)
, s(State::NotYetAnalyzed)
, returnstate(ReturnState::NoReturnSeen) {
    // Only match the non-concrete arguments.
    variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the argument scope
    unsigned num = 0;
    unsigned metaargs = 0;
    if (mem && dynamic_cast<UserDefinedType*>(mem->Decay()))
        Args.push_back(mem == mem->Decay() ? a.GetLvalueType(mem) : mem);
    for(auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
#pragma warning(disable : 4800)
        auto param = [this, num] { return num + ReturnType->IsComplexType() + (bool(dynamic_cast<UserDefinedType*>(context))); };
        Type* ty = args[num];
        // Expr is set.
        ConcreteExpression var(a.GetLvalueType(ty), nullptr);
        if (ty->IsComplexType()) {
            var.Expr = a.gen->CreateParameterExpression(param);
        } else {
            std::vector<ConcreteExpression> args;
            ConcreteExpression copy(ty, a.gen->CreateParameterExpression(param));
            args.push_back(copy);
            if (copy.t->IsReference())
                var.Expr = copy.Expr;
            else
                var.Expr = ty->BuildLvalueConstruction(args, a, arg.location).Expr;
        }
        ++num;
        variables.back().insert(std::make_pair(arg.name, var));
        exprs.push_back(var.Expr);
        Args.push_back(ty);
    }

    std::stringstream strstr;
    strstr << "__" << std::hex << this;
    name = strstr.str();

    // Deal with the prolog first.
    for(auto&& prolog : astfun->prolog) {
        auto ass = dynamic_cast<const AST::BinaryExpression*>(prolog);
        if (!ass || ass->type != Lexer::TokenType::Assignment)            
            throw std::runtime_error("Prologs can only be composed of assignment expressions right now!");
        auto ident = dynamic_cast<const AST::Identifier*>(ass->lhs);
        if (!ident)
            throw std::runtime_error("Prolog assignment expressions must have a plain identifier on the left-hand-side right now!");
        auto expr = a.AnalyzeExpression(this, ass->rhs).Resolve(nullptr);
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
            returnstate = ReturnState::ConcreteReturnSeen;
        }
    }
    if (ReturnType) {
        // We have the name and signature- declare, and generate the body later.
        codefun = a.gen->CreateFunction(GetSignature(a)->GetLLVMType(a), name, this);
    }
}

clang::QualType Function::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    return GetSignature(a)->GetClangType(where, a);
}

Expression check(Wide::Util::optional<Expression> e) {
    if (!e)
        assert(false && "Expected to find this thing for sure, but it didn't exist! Check call stack for trigger.");
    return *e;
}
void Function::ComputeBody(Analyzer& a) {
    auto member = dynamic_cast<UserDefinedType*>(context);
    if (s == State::NotYetAnalyzed) {
        s = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor
        if (member && fun->name == "type") {
            ConcreteExpression self(a.GetLvalueType(member), a.gen->CreateParameterExpression([this] { return ReturnType->IsComplexType(); }));
            auto members = member->GetMembers();
            for (auto&& x : members) {
                auto has_initializer = [&](std::string name) -> const AST::Variable* {
                    for (auto&& x : fun->initializers)
                    if (x->name == name)
                        return x;
                    return nullptr;
                };
                if (auto init = has_initializer(x.name)) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                    std::vector<ConcreteExpression> args;
                    if (init->initializer)
                        args.push_back(a.AnalyzeExpression(this, init->initializer).Resolve(nullptr));
                    exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), a, init->location));
                }
                else {
                    // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                    if (x.InClassInitializer)
                    {
                        auto expr = a.AnalyzeExpression(this, x.InClassInitializer).Resolve(nullptr);
                        auto mem = check(self.t->AccessMember(self, x.name, a, x.InClassInitializer->location)).Resolve(nullptr);
                        std::vector<ConcreteExpression> args;
                        args.push_back(expr);
                        exprs.push_back(x.t->BuildInplaceConstruction(mem.Expr, std::move(args), a, x.InClassInitializer->location));
                        continue;
                    }
                    if (x.t->IsReference())
                        throw std::runtime_error("Failed to initialize a reference member.");
                    auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                    std::vector<ConcreteExpression> args;
                    exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), a, fun->where.front()));
                }
            }
        }
        // Now the body.
        variables.push_back(std::unordered_map<std::string, Expression>()); // Push back the function body scope.
        std::function<Codegen::Statement*(const AST::Statement*)> AnalyzeStatement;
        AnalyzeStatement = [&](const AST::Statement* stmt) -> Codegen::Statement* {
            if (!stmt)
                return nullptr;
        
            if (auto e = dynamic_cast<const AST::Expression*>(stmt))
                return analyzer.AnalyzeExpression(this, e).VisitContents(
                    [](ConcreteExpression& expr) {
                        return expr.Expr;
                    },
                    [&](DeferredExpression& delay) {
                        return a.gen->CreateDeferredStatement([=] {
                            return delay(nullptr).Expr;
                        });
                    }
               );
        
            if (auto ret = dynamic_cast<const AST::Return*>(stmt)) {                
                if (!ret->RetExpr) {
                    returnstate = ReturnState::ConcreteReturnSeen;
                    ReturnType = a.GetVoidType();
                    return a.gen->CreateReturn();
                }
                else {
                    // When returning an lvalue or rvalue, decay them.
                    auto expr = a.AnalyzeExpression(this, ret->RetExpr);
                    return expr.VisitContents([&](ConcreteExpression& result) {
                        if (returnstate == ReturnState::ConcreteReturnSeen) {
                            if (ReturnType != result.t->Decay()) {
                                throw std::runtime_error("Return mismatch: In a deduced function, all returns must return the same type.");
                            }
                        }
                        returnstate = ReturnState::ConcreteReturnSeen;                        
                        if (!ReturnType)
                            ReturnType = result.t->Decay();
        
                        // Deal with emplacing the result
                        if (ReturnType->IsComplexType()) {
                            std::vector<ConcreteExpression> args;
                            args.push_back(result);
                            return a.gen->CreateReturn(ReturnType->BuildInplaceConstruction(a.gen->CreateParameterExpression(0), std::move(args), a, ret->location));
                        }
                        else
                            return a.gen->CreateReturn(result.t->BuildValue(result, a, ret->location).Expr);
                    }, [&](DeferredExpression& expr) {
                        if (returnstate == ReturnState::NoReturnSeen)
                            returnstate = ReturnState::DeferredReturnSeen;
                        auto curr = *expr.delay;
                        *expr.delay = [=, &a](Type* t) {
                            CompleteAnalysis(t, a);
                            return curr(ReturnType);
                        };
                        return a.gen->CreateReturn([=]() mutable {
                            return expr(nullptr).Expr;
                        });
                    });
                }
            }
            if (auto var = dynamic_cast<const AST::Variable*>(stmt)) {
                auto result = a.AnalyzeExpression(this, var->initializer);
        
                for (auto scope = variables.begin(); scope != variables.end(); ++scope) {
                    if (scope->find(var->name) != scope->end())
                        throw std::runtime_error("Error: variable shadowing " + var->name);
                }

                auto handle_variable_expression = [&, var](ConcreteExpression result) {
                    std::vector<ConcreteExpression> args;
                    args.push_back(result);
                    
                    if (result.t->IsReference()) {
                        if (!result.steal) {
                            result.t = a.GetLvalueType(result.t);
                            return result.t->Decay()->BuildLvalueConstruction(args, a, var->location);
                        } else {
                            result.steal = false;
                            // If I've been asked to steal an lvalue, it's because I rvalue constructed an lvalue reference
                            // So the expr will be T** whereas I want just T*
                            //if (dynamic_cast<LvalueType*>(result.t))
                            //    result.Expr = a.gen->CreateLoad(result.Expr);
                            result.t = a.GetLvalueType(result.t);
                            return result;
                        }
                    } else {
                        return result.t->BuildLvalueConstruction(args, a, var->location);
                    }
                };

                return result.VisitContents(
                    [&, handle_variable_expression](ConcreteExpression& result) {
                        auto expr = handle_variable_expression(result);
                        variables.back().insert(std::make_pair(var->name, expr));
                        return expr.Expr;
                    }, 
                    [&, handle_variable_expression](DeferredExpression& result) {
                        auto defexpr = DeferredExpression([result, handle_variable_expression](Type* t) {
                            return handle_variable_expression(result(t));
                        });
                        variables.back().insert(std::make_pair(var->name, defexpr));
                        return a.gen->CreateDeferredStatement([=] {
                            return defexpr(nullptr).Expr;
                        });
                    }
                );
        
                // If it's a function call, and it returns a complex T, then the return expression already points to
                // a memory variable of type T. Just use that.
            }
        
            if (auto comp = dynamic_cast<const AST::CompoundStatement*>(stmt)) {
                variables.push_back(std::unordered_map<std::string, Expression>());
                if (comp->stmts.size() == 0)
                    return a.gen->CreateNop();
                Codegen::Statement* chain = a.gen->CreateNop();
                for (auto&& x : comp->stmts) {
                    chain = a.gen->CreateChainStatement(chain, AnalyzeStatement(x));
                }
                variables.pop_back();
                return chain;
            }
        
            if (auto if_stmt = dynamic_cast<const AST::If*>(stmt)) {
                auto cond = a.AnalyzeExpression(this, if_stmt->condition);
                auto expr = cond.BuildBooleanConversion(a, if_stmt->condition->location);
                auto true_br = AnalyzeStatement(if_stmt->true_statement);
                auto false_br =  AnalyzeStatement(if_stmt->false_statement);
                return expr.VisitContents(
                    [&](ConcreteExpression& expr) {
                        return a.gen->CreateIfStatement(expr.Expr, true_br, false_br);
                    },
                    [&](DeferredExpression& expr) {
                        return a.gen->CreateIfStatement([=] { return expr(nullptr).Expr; }, true_br, false_br);
                    }
                );                
            }
        
            if (auto while_stmt = dynamic_cast<const AST::While*>(stmt)) {
                auto cond = a.AnalyzeExpression(this, while_stmt->condition);
                auto expr = cond.BuildBooleanConversion(a, while_stmt->condition->location);
                auto body = AnalyzeStatement(while_stmt->body);                
                return expr.VisitContents(
                    [&](ConcreteExpression& expr) {
                        return a.gen->CreateWhile(expr.Expr, body);
                    },
                    [&](DeferredExpression& expr) {
                        return a.gen->CreateWhile([=] { return expr(nullptr).Expr; }, body);
                    }
                );                
            }
        
            throw std::runtime_error("Unsupported statement.");
        };
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            exprs.push_back(AnalyzeStatement(fun->statements[i]));
        }        
        // Prevent infinite recursion by setting this variable as soon as the return type is ready
        // Else GetLLVMType() will call us again to prepare the return type.
        
        if (returnstate == ReturnState::NoReturnSeen)
            CompleteAnalysis(nullptr, a);
        if (returnstate == ReturnState::ConcreteReturnSeen)
            CompleteAnalysis(ReturnType, a);
    }
}
void Function::CompleteAnalysis(Type* ret, Analyzer& a) {
    if (ReturnType)
        if (ret != ReturnType)
            throw std::runtime_error("Attempted to resolve a function with a return type, but it was already resolved with a different return type.");
    ReturnType = ret;
    if (!ReturnType) { ReturnType = a.GetVoidType(); exprs.push_back(a.gen->CreateReturn()); }
    s = State::AnalyzeCompleted;
    if (ReturnType == a.GetVoidType() && !dynamic_cast<Codegen::ReturnStatement*>(exprs.back()))
        exprs.push_back(a.gen->CreateReturn());
    if (!codefun)
        codefun = a.gen->CreateFunction(GetSignature(a)->GetLLVMType(a), name, this);
    for (auto&& x : exprs)
        codefun->AddStatement(x);
    variables.clear();    
}
Expression Function::BuildCall(ConcreteExpression ex, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (s == State::AnalyzeCompleted) {
        ConcreteExpression val(this, a.gen->CreateFunctionValue(name));
        return GetSignature(a)->BuildCall(val, std::move(args), a, where);
    } 
    if (s == State::NotYetAnalyzed) {
        ComputeBody(a);
        return BuildCall(ex, std::move(args), a, where);
    }
    return DeferredExpression([this, &a, args, where](Type* t) {
        if (s != State::AnalyzeCompleted)
            CompleteAnalysis(t, a);
        ConcreteExpression val(this, a.gen->CreateFunctionValue(name));
        return GetSignature(a)->BuildCall(val, std::move(args), a, where).Resolve(t);
    });    
}
Wide::Util::optional<Expression> Function::LookupLocal(std::string name, Analyzer& a, Lexer::Range where) {
    for(auto it = variables.rbegin(); it != variables.rend(); ++it) {
        auto&& map = *it;
        if (map.find(name) != map.end()) {
            return map.at(name);
        }
    }
    if (name == "this" && dynamic_cast<UserDefinedType*>(context))
        return ConcreteExpression(a.GetLvalueType(context), a.gen->CreateParameterExpression([this] { return ReturnType->IsComplexType(); }));
    return Wide::Util::none;
}
std::string Function::GetName() {
    return name;
}
FunctionType* Function::GetSignature(Analyzer& a) {
    if (s == State::NotYetAnalyzed)
        ComputeBody(a);
    if (s == State::AnalyzeInProgress)
        assert(false && "Attempted to call GetSignature whilst a function was still being analyzed.");
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

bool Function::AddThis() {
    return dynamic_cast<UserDefinedType*>(context->Decay());
}