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
, returnstate(ReturnState::NoReturnSeen)
, root_scope(nullptr) {
    // Only match the non-concrete arguments.
    current_scope = &root_scope;
    unsigned num = 0;
    unsigned metaargs = 0;
    if (mem && dynamic_cast<UserDefinedType*>(mem->Decay())) {
        ++num; // Skip the first argument in args if we are a member.
        Args.push_back(mem == mem->Decay() ? a.GetLvalueType(mem) : mem);
    }
    for(auto&& arg : astfun->args) {
        Context c(a, arg.location, [this](ConcreteExpression e) {
           current_scope->needs_destruction.push_back(e);
        });
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
                var.Expr = ty->BuildLvalueConstruction(args, c).Expr;
        }
        ++num;
        current_scope->named_variables.insert(std::make_pair(arg.name, var));
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
        auto expr = a.AnalyzeExpression(this, ass->rhs, [](ConcreteExpression e) {}).Resolve(nullptr);
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
    auto member = dynamic_cast<UserDefinedType*>(context->Decay());
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
                // For member variables, don't add them to the list, the destructor will handle them.
                if (auto init = has_initializer(x.name)) {
                    Context c(a, init->location, [](ConcreteExpression e) {});
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                    std::vector<ConcreteExpression> args;
                    if (init->initializer)
                        args.push_back(a.AnalyzeExpression(this, init->initializer, [](ConcreteExpression e) {}).Resolve(nullptr));
                    exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), c));
                }
                else {
                    // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                    if (x.InClassInitializer)
                    {
                        Context c(a, x.InClassInitializer->location, [](ConcreteExpression e) {});
                        auto expr = a.AnalyzeExpression(this, x.InClassInitializer, [](ConcreteExpression e) {}).Resolve(nullptr);
                        auto mem = check(self.t->AccessMember(self, x.name, c)).Resolve(nullptr);
                        std::vector<ConcreteExpression> args;
                        args.push_back(expr);
                        exprs.push_back(x.t->BuildInplaceConstruction(mem.Expr, std::move(args), c));
                        continue;
                    }
                    if (x.t->IsReference())
                        throw std::runtime_error("Failed to initialize a reference member.");
                    auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                    std::vector<ConcreteExpression> args;
                    Context c(a, fun->where.front(), [](ConcreteExpression e) {});
                    exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), c));
                }
            }
            for(auto&& x : fun->initializers)
                if (std::find_if(members.begin(), members.end(), [&](decltype(*members.begin())& ref) { return ref.name == x->name; }) == members.end())
                    throw std::runtime_error("Attempted to initialize a member that did not exist.");
        }
        // Now the body.
        root_scope.children.push_back(Wide::Memory::MakeUnique<Scope>(&root_scope));
        auto destroy_locals = [](Context c, Scope* scope) {
            Codegen::Expression* destructors = c->gen->CreateNop();
            while(scope) {
                for(auto var = scope->needs_destruction.rbegin(); var != scope->needs_destruction.rend(); ++var) {
                    destructors = c->gen->CreateChainExpression(destructors, var->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                }
                scope = scope->parent;
            }
            return destructors;
        };
        std::function<Codegen::Statement*(const AST::Statement*, Scope*)> AnalyzeStatement;
        AnalyzeStatement = [&](const AST::Statement* stmt, Scope* scope) -> Codegen::Statement* {
            auto call_destructors = [destroy_locals](Context c, Scope* scope) { return c->gen->CreateDeferredExpression([c, scope, destroy_locals] { return destroy_locals(c, scope); }); };

            auto register_local_destructor = [scope](ConcreteExpression obj) { scope->needs_destruction.push_back(obj); };
            if (!stmt)
                return nullptr;
        
            if (auto e = dynamic_cast<const AST::Expression*>(stmt))
                return analyzer.AnalyzeExpression(this, e, register_local_destructor).VisitContents(
                    [](ConcreteExpression& expr) {
                        return expr.Expr;
                    },
                    [&](DeferredExpression& delay) {
                        return a.gen->CreateDeferredExpression([=] {
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
                    // Destroy all temporaries involved.
                    auto expr = a.AnalyzeExpression(this, ret->RetExpr, register_local_destructor);
                    auto handle_result = [this, ret, &a, call_destructors, scope](ConcreteExpression result) {
                        if (returnstate == ReturnState::ConcreteReturnSeen) {
                            if (ReturnType != result.t->Decay()) {
                                throw std::runtime_error("Return mismatch: In a deduced function, all returns must return the same type.");
                            }
                        }
                        returnstate = ReturnState::ConcreteReturnSeen;                        
                        if (!ReturnType)
                            ReturnType = result.t->Decay();
        
                        // Deal with emplacing the result
                        // If we emplace a complex result, don't destruct it, because that's the callee's job.
                        Context c(a, ret->location, [](ConcreteExpression e) {});
                        if (ReturnType->IsComplexType()) {
                            std::vector<ConcreteExpression> args;
                            args.push_back(result);
                            auto retexpr = ReturnType->BuildInplaceConstruction(a.gen->CreateParameterExpression(0), std::move(args), c);
                            return a.gen->CreateChainExpression(a.gen->CreateChainExpression(retexpr, call_destructors(c, scope)), retexpr);
                        } else {
                            auto ret = result.t->BuildValue(result, c).Expr;
                            return a.gen->CreateChainExpression(a.gen->CreateChainExpression(ret, call_destructors(c, scope)), ret);
                        }
                    };
                    return expr.VisitContents(
                        [&](ConcreteExpression& expr) { 
                            return a.gen->CreateReturn(handle_result(expr)); 
                        }, 
                        [&, handle_result](DeferredExpression& expr) {
                            if (returnstate == ReturnState::NoReturnSeen)
                                returnstate = ReturnState::DeferredReturnSeen;
                            auto curr = *expr.delay;
                            *expr.delay = [=, &a](Type* t) {
                                auto f = curr(t);
                                if (s != State::AnalyzeCompleted)
                                    CompleteAnalysis(f.t, a);
                                return f;
                            };
                            return a.gen->CreateReturn([=]() mutable {
                                return handle_result(expr(nullptr));
                            });
                        }
                    );
                }
            }
            if (auto var = dynamic_cast<const AST::Variable*>(stmt)) {
                auto result = a.AnalyzeExpression(this, var->initializer, register_local_destructor);
        
                auto currscope = scope;
                while(currscope) {
                    if (currscope->named_variables.find(var->name) != currscope->named_variables.end())
                        throw std::runtime_error("Error: variable shadowing " + var->name);
                    currscope = currscope->parent;
                }

                auto handle_variable_expression = [var, register_local_destructor, &a](ConcreteExpression result) {
                    std::vector<ConcreteExpression> args;
                    args.push_back(result);
                    Context c(a, var->location, register_local_destructor);
                    if (result.t->IsReference()) {
                        if (!result.steal) {
                            result.t = a.GetLvalueType(result.t);
                            return result.t->Decay()->BuildLvalueConstruction(args, c);
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
                        return result.t->BuildLvalueConstruction(args, c);
                    }
                };

                return result.VisitContents(
                    [&, handle_variable_expression, scope](ConcreteExpression& result) {
                        auto expr = handle_variable_expression(result);
                        scope->named_variables.insert(std::make_pair(var->name, expr));
                        return expr.Expr;
                    }, 
                    [&, handle_variable_expression, scope](DeferredExpression& result) {
                        auto defexpr = DeferredExpression([result, handle_variable_expression](Type* t) {
                            return handle_variable_expression(result(t));
                        });
                        scope->named_variables.insert(std::make_pair(var->name, defexpr));
                        return a.gen->CreateDeferredExpression([=] {
                            return defexpr(nullptr).Expr;
                        });
                    }
                );
        
                // If it's a function call, and it returns a complex T, then the return expression already points to
                // a memory variable of type T. Just use that.
            }
        
            if (auto comp = dynamic_cast<const AST::CompoundStatement*>(stmt)) {
                scope->children.push_back(Wide::Memory::MakeUnique<Scope>(scope));
                current_scope = scope->children.back().get();
                Context c(a, comp->location, [](ConcreteExpression e) {});
                if (comp->stmts.size() == 0)
                    return a.gen->CreateNop();
                Codegen::Statement* chain = a.gen->CreateNop();
                for (auto&& x : comp->stmts) {
                    chain = a.gen->CreateChainStatement(chain, AnalyzeStatement(x, current_scope));
                }
                for(auto des = current_scope->needs_destruction.rbegin(); des != current_scope->needs_destruction.rend(); ++des)
                    chain = a.gen->CreateChainStatement(chain, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                current_scope = scope;
                return chain;
            }
        
            if (auto if_stmt = dynamic_cast<const AST::If*>(stmt)) {
                // condition
                //     true
                //     false
                scope->children.push_back(Wide::Memory::MakeUnique<Scope>(scope));
                auto condscope = current_scope = scope->children.back().get();
                Context c(a, if_stmt->condition ? if_stmt->condition->location : if_stmt->var_condition->location, [=](ConcreteExpression e) { current_scope->needs_destruction.push_back(e); });
                Expression cond = if_stmt->var_condition
                    ? AnalyzeStatement(if_stmt->var_condition, condscope), condscope->named_variables.begin()->second.BuildBooleanConversion(c)
                    : a.AnalyzeExpression(this, if_stmt->condition, [=](ConcreteExpression e) { current_scope->needs_destruction.push_back(e); }).BuildBooleanConversion(c);
                auto expr = cond.BuildBooleanConversion(c);
                condscope->children.push_back(Wide::Memory::MakeUnique<Scope>(condscope));
                current_scope = condscope->children.back().get();
                auto true_br = AnalyzeStatement(if_stmt->true_statement, current_scope);
                // add any locals.
                c.where = if_stmt->true_statement->location;
                for(auto des = current_scope->needs_destruction.rbegin(); des != current_scope->needs_destruction.rend(); ++des)
                    true_br = a.gen->CreateChainStatement(true_br, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                if (if_stmt->false_statement)
                    c.where = if_stmt->false_statement->location;
                condscope->children.push_back(Wide::Memory::MakeUnique<Scope>(condscope));
                current_scope = condscope->children.back().get();
                auto false_br = AnalyzeStatement(if_stmt->false_statement, current_scope);
                // add any locals.
                for (auto des = current_scope->needs_destruction.rbegin(); des != current_scope->needs_destruction.rend(); ++des)
                    false_br = a.gen->CreateChainStatement(false_br, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                current_scope = scope;
                return expr.VisitContents(
                    [&](ConcreteExpression& expr) {
                        auto cleanup = (Codegen::Statement*)a.gen->CreateNop();
                        for(auto des = condscope->needs_destruction.rbegin(); des != condscope->needs_destruction.rend(); ++des)
                            cleanup = a.gen->CreateChainStatement(cleanup, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);

                        return a.gen->CreateChainStatement(a.gen->CreateIfStatement(expr.Expr, true_br, false_br), cleanup);
                    },
                    [&](DeferredExpression& expr) {
                        return a.gen->CreateChainStatement(
                            a.gen->CreateIfStatement([=] { return expr(nullptr).Expr; }, true_br, false_br), 
                            a.gen->CreateDeferredStatement([=, &a] {
                                auto cleanup = (Codegen::Statement*)a.gen->CreateNop();
                                for(auto des = condscope->needs_destruction.rbegin(); des != condscope->needs_destruction.rend(); ++des)
                                    cleanup = a.gen->CreateChainStatement(cleanup, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                                return cleanup;
                            })
                        );
                    }
                );                
            }
        
            if (auto while_stmt = dynamic_cast<const AST::While*>(stmt)) {
                Context c(a, while_stmt->condition ? while_stmt->condition->location : while_stmt->var_condition->location, [=](ConcreteExpression e) { current_scope->needs_destruction.push_back(e); });
                scope->children.push_back(Wide::Memory::MakeUnique<Scope>(scope));
                auto condscope = current_scope = scope->children.back().get();
                Expression cond = while_stmt->var_condition
                    ? AnalyzeStatement(while_stmt->var_condition, condscope), condscope->named_variables.begin()->second.BuildBooleanConversion(c)
                    : a.AnalyzeExpression(this, while_stmt->condition, [=](ConcreteExpression e) { current_scope->needs_destruction.push_back(e); }).BuildBooleanConversion(c) ;
                auto ret = condscope->current_while = cond.VisitContents(
                    [&](ConcreteExpression& expr) {
                        return a.gen->CreateWhile(expr.Expr);
                    },
                    [&](DeferredExpression& expr) {
                        return a.gen->CreateWhile([=] { return expr(nullptr).Expr; });
                    }
                );    
                condscope->children.push_back(Wide::Memory::MakeUnique<Scope>(condscope));
                current_scope = condscope->children.back().get();
                auto body = AnalyzeStatement(while_stmt->body, current_scope);  
                current_scope = scope;
                condscope->current_while = nullptr;
                ret->SetBody(body);
                return a.gen->CreateChainStatement(ret, a.gen->CreateDeferredStatement([=, &a] {
                    Codegen::Statement* cleanup = a.gen->CreateNop();
                    for(auto des = condscope->needs_destruction.rbegin(); des != condscope->needs_destruction.rend(); ++des)
                        cleanup = a.gen->CreateChainStatement(cleanup, des->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                    return cleanup;
                }));
            }

            if (auto break_stmt = dynamic_cast<const AST::Break*>(stmt)) {
                Codegen::Statement* des = a.gen->CreateNop();
                auto currscope = scope;
                Context c(a, break_stmt->location, [](ConcreteExpression e) {});
                while(currscope) {
                    for(auto var = currscope->needs_destruction.rbegin(); var != currscope->needs_destruction.rend(); ++var)
                        des = a.gen->CreateChainStatement(des, var->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                    if (currscope->current_while)
                        return a.gen->CreateChainStatement(des, a.gen->CreateBreak(currscope->current_while));
                    currscope = currscope->parent;
                }
                throw std::runtime_error("Used a break but could not find a control flow statement to break out of.");
            }

            if (auto continue_stmt = dynamic_cast<const AST::Continue*>(stmt)) {
                Codegen::Statement* des = a.gen->CreateNop();
                auto currscope = scope;
                Context c(a, continue_stmt->location, [](ConcreteExpression e) {});
                while(currscope) {
                    for(auto var = currscope->needs_destruction.rbegin(); var != currscope->needs_destruction.rend(); ++var)
                        des = a.gen->CreateChainStatement(des, var->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
                    if (currscope->current_while)
                        return a.gen->CreateChainStatement(des, a.gen->CreateContinue(currscope->current_while));
                    currscope = currscope->parent;
                }
                throw std::runtime_error("Used a continue but could not find a control flow statement to break out of.");
            }
        
            throw std::runtime_error("Unsupported statement.");
        };
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            exprs.push_back(AnalyzeStatement(fun->statements[i], current_scope));
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
    if (returnstate == ReturnState::NoReturnSeen)
        ReturnType = a.GetVoidType();
    else
        if (!ReturnType)
            throw std::runtime_error("Deferred return type was not evaluated prior to completing analysis.");
    if (ReturnType == a.GetVoidType() && !dynamic_cast<Codegen::ReturnStatement*>(exprs.empty() ? nullptr : exprs.back())) {
        Context c(a, fun->where.front(), [](ConcreteExpression e) { assert(false); });
        auto currscope = current_scope;
        while(currscope) {
            for(auto var = currscope->needs_destruction.rbegin(); var != currscope->needs_destruction.rend(); ++var) {
                exprs.push_back(var->AccessMember("~type", c)->BuildCall(c).Resolve(nullptr).Expr);
            }
            currscope = currscope->parent;
        }
        exprs.push_back(a.gen->CreateReturn()); 
    }
    s = State::AnalyzeCompleted;
    if (!codefun)
        codefun = a.gen->CreateFunction(GetSignature(a)->GetLLVMType(a), name, this);
    for (auto&& x : exprs)
        codefun->AddStatement(x);
}
Expression Function::BuildCall(ConcreteExpression ex, std::vector<ConcreteExpression> args, Context c) {
    if (s == State::AnalyzeCompleted) {
        ConcreteExpression val(this, c->gen->CreateFunctionValue(name));
        return GetSignature(*c)->BuildCall(val, std::move(args), c);
    } 
    if (s == State::NotYetAnalyzed) {
        ComputeBody(*c);
        return BuildCall(ex, std::move(args), c);
    }
    return DeferredExpression([this, c, args](Type* t) {
        if (s != State::AnalyzeCompleted)
            CompleteAnalysis(t, *c);
        ConcreteExpression val(this, c->gen->CreateFunctionValue(name));
        return GetSignature(*c)->BuildCall(val, std::move(args), c).Resolve(t);
    });    
}
Wide::Util::optional<Expression> Function::LookupLocal(std::string name, Context c) {
    auto currscope = current_scope;
    while(currscope) {
        if (currscope->named_variables.find(name) != currscope->named_variables.end()) {
            return currscope->named_variables.at(name);
        }
        currscope = currscope->parent;
    }
    if (name == "this" && dynamic_cast<UserDefinedType*>(context->Decay()))
        return ConcreteExpression(c->GetLvalueType(context->Decay()), c->gen->CreateParameterExpression([this] { return ReturnType->IsComplexType(); }));
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
    auto currscope = current_scope;
    while(currscope) {
        if (currscope->named_variables.find(name) != currscope->named_variables.end()) {
            return true;
        }
        currscope = currscope->parent;
    }
    return false;
}

bool Function::AddThis() {
    return dynamic_cast<UserDefinedType*>(context->Decay());
}
Type* Function::GetConstantContext(Analyzer& a) {
    struct FunctionConstantContext : public MetaType {
        Type* context;
        std::unordered_map<std::string, Expression> constant;
        Type* GetContext(Analyzer& a) override final {
            return context;
        }
        Wide::Util::optional<Expression> AccessMember(ConcreteExpression e, std::string member, Context c) override final {
            if (constant.find(member) == constant.end())
                return Wide::Util::none;
            return constant.at(member).VisitContents(
                [&](ConcreteExpression& e) -> Wide::Util::optional<Expression> {
                    return e.t->Decay()->GetConstantContext(*c)->BuildValueConstruction(c);
                },
                [&](DeferredExpression& e) {
                    auto expr = e(nullptr);
                    return expr.t->GetConstantContext(*c) ? Wide::Util::optional<Expression>(expr.t->GetConstantContext(*c)->BuildValueConstruction(c)) : Wide::Util::none;
                }
            );
        }
    };
    auto context = a.arena.Allocate<FunctionConstantContext>();
    context->context = GetContext(a);
    for(auto scope = current_scope; scope; scope = scope->parent) {
        for (auto&& var : scope->named_variables) {
            if (context->constant.find(var.first) == context->constant.end()) {
                var.second.VisitContents(
                    [&](ConcreteExpression& e) {
                        if (e.t->Decay()->GetConstantContext(a))
                            context->constant.insert(std::make_pair(var.first, e));
                    },
                    [&](DeferredExpression& e) {
                        context->constant.insert(std::make_pair(var.first, e));
                    }
                 );
             }
        }
    }
    return context;
}