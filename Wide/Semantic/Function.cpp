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
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/LambdaType.h>
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

struct Function::LocalScope {
    Scope* parent;
    Scope* oldcurrent;
    Scope* self;
    Function* func;
    LocalScope(Scope* p, Function* f)
        : parent(p), oldcurrent(f->current_scope), func(f)
    {
        parent->children.push_back(Wide::Memory::MakeUnique<Scope>(parent));
        self = parent->children.back().get();
        f->current_scope = self;        
    }
    Scope* operator->() {
        return self;
    }
    Scope* get() {
        return self;
    }
    ~LocalScope() {
        func->current_scope = oldcurrent;
    }
};
Codegen::Statement* Function::Scope::GetCleanupExpression(Analyzer& a, Lexer::Range where) {
    Context c(a, where, [](ConcreteExpression x) {});
    Codegen::Statement* chain = a.gen->CreateNop();
    for (auto des = needs_destruction.rbegin(); des != needs_destruction.rend(); ++des) {
        std::vector<Type*> types;
        types.push_back(des->t);
        chain = a.gen->CreateChainStatement(chain, des->t->Decay()->GetDestructorOverloadSet(*c)->Resolve(types, *c)->Call(*des, c).Expr);
        chain = a.gen->CreateChainStatement(chain, a.gen->CreateLifetimeEnd(des->Expr));
    }
    return chain;
}
Codegen::Statement* Function::Scope::GetCleanupAllExpression(Analyzer& a, Lexer::Range where) {
    if (parent)
        return a.gen->CreateChainStatement(GetCleanupExpression(a, where), parent->GetCleanupAllExpression(a, where));
    return GetCleanupExpression(a, where);
}
Wide::Util::optional<ConcreteExpression> Function::Scope::LookupName(std::string name) {
    if (named_variables.find(name) != named_variables.end())
        return named_variables.at(name);
    if (parent)
        return parent->LookupName(name);
    return Wide::Util::none;
}

Function::Function(std::vector<Type*> args, const AST::FunctionBase* astfun, Analyzer& a, Type* mem)
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
    if (mem && (dynamic_cast<UserDefinedType*>(mem->Decay()) || dynamic_cast<LambdaType*>(mem->Decay()))) {
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
        auto param = [this, num, &a] { return num + ReturnType->IsComplexType(a) + (bool(dynamic_cast<UserDefinedType*>(context))); };
        Type* ty = args[num];
        // Expr is set.
        ConcreteExpression var(a.GetLvalueType(ty), nullptr);
        if (ty->IsComplexType(a)) {
            var.Expr = a.gen->CreateParameterExpression(param);
        } else {
            if (ty->IsReference()) {
                var.Expr = a.gen->CreateParameterExpression(param);
                var.t = ty;
            } else {
                std::vector<ConcreteExpression> args;
                ConcreteExpression copy(ty, a.gen->CreateParameterExpression(param));
                args.push_back(copy);
                if (copy.t->IsReference())
                    var.Expr = copy.Expr;
                else
                    var.Expr = ty->BuildLvalueConstruction(args, c).Expr;
            }
        }
        ++num;
        current_scope->named_variables.insert(std::make_pair(arg.name, var));
        exprs.push_back(var.Expr);
        Args.push_back(ty);
    }

    std::stringstream strstr;
    strstr << "__" << std::hex << this;
    name = strstr.str();

    // Deal with the prolog first- if we have one.
    if (auto fun = dynamic_cast<const AST::Function*>(astfun)) {
        for (auto&& prolog : fun->prolog) {
            auto ass = dynamic_cast<const AST::BinaryExpression*>(prolog);
            if (!ass || ass->type != Lexer::TokenType::Assignment)
                throw std::runtime_error("Prologs can only be composed of assignment expressions right now!");
            auto ident = dynamic_cast<const AST::Identifier*>(ass->lhs);
            if (!ident)
                throw std::runtime_error("Prolog assignment expressions must have a plain identifier on the left-hand-side right now!");
            auto expr = a.AnalyzeExpression(this, ass->rhs, [](ConcreteExpression e) {});
            if (ident->val == "ExportName") {
                auto str = dynamic_cast<Codegen::StringExpression*>(expr.Expr);
                if (!str)
                    throw std::runtime_error("Prolog right-hand-sides of ExportName must be of string type!");
                trampoline = str->GetContents();
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
}

clang::QualType Function::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    return GetSignature(a)->GetClangType(where, a);
}

ConcreteExpression check(Wide::Util::optional<ConcreteExpression> e) {
    if (!e)
        assert(false && "Expected to find this thing for sure, but it didn't exist! Check call stack for trigger.");
    return *e;
}
void Function::ComputeBody(Analyzer& a) {
    auto member = dynamic_cast<UserDefinedType*>(context->Decay());
    if (s == State::NotYetAnalyzed) {
        s = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor
        if (member) {
            if (auto con = dynamic_cast<const AST::Constructor*>(fun)) {
                ConcreteExpression self(a.GetLvalueType(member), a.gen->CreateParameterExpression([this, &a] { return ReturnType->IsComplexType(a); }));
                auto members = member->GetMembers();
                for (auto&& x : members) {
                    auto has_initializer = [&](std::string name) -> const AST::Variable*{
                    for (auto&& x : con->initializers) {
                        // Can only have 1 name- AST restriction
                        assert(x->name.size() == 1);
                        if (x->name.front() == name)
                            return x;
                    }
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
                            args.push_back(a.AnalyzeExpression(this, init->initializer, [](ConcreteExpression e) {}));
                        exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), c));
                    } else {
                        // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                        /*if (x.InClassInitializer)
                        {
                        Context c(a, x.InClassInitializer->location, [](ConcreteExpression e) {});
                        auto expr = a.AnalyzeExpression(this, x.InClassInitializer, [](ConcreteExpression e) {});
                        auto mem = check(self.t->AccessMember(self, x.name, c));
                        std::vector<ConcreteExpression> args;
                        args.push_back(expr);
                        exprs.push_back(x.t->BuildInplaceConstruction(mem.Expr, std::move(args), c));
                        continue;
                        }*/
                        if (x.t->IsReference())
                            throw std::runtime_error("Failed to initialize a reference member.");
                        auto mem = a.gen->CreateFieldExpression(self.Expr, x.num);
                        std::vector<ConcreteExpression> args;
                        Context c(a, con->where(), [](ConcreteExpression e) {});
                        exprs.push_back(x.t->BuildInplaceConstruction(mem, std::move(args), c));
                    }
                }
                for (auto&& x : con->initializers)
                    if (std::find_if(members.begin(), members.end(), [&](decltype(*members.begin())& ref) { return ref.name == x->name.front(); }) == members.end())
                        throw std::runtime_error("Attempted to initialize a member that did not exist.");
            }
        }
        // Now the body.
        LocalScope base_scope(&root_scope, this);
        std::function<Codegen::Statement*(const AST::Statement*, Scope*)> AnalyzeStatement;
        AnalyzeStatement = [&](const AST::Statement* stmt, Scope* scope) -> Codegen::Statement* {
            auto register_local_destructor = [this](ConcreteExpression obj) { 
                current_scope->needs_destruction.push_back(obj); 
            };
            if (!stmt)
                return nullptr;
        
            if (auto e = dynamic_cast<const AST::Expression*>(stmt))
                return analyzer.AnalyzeExpression(this, e, register_local_destructor).Expr;
        
            if (auto ret = dynamic_cast<const AST::Return*>(stmt)) {                
                if (!ret->RetExpr) {
                    returnstate = ReturnState::ConcreteReturnSeen;
                    ReturnType = a.GetVoidType();
                    return a.gen->CreateReturn();
                }
                else {
                    // When returning an lvalue or rvalue, decay them.
                    // Destroy all temporaries involved.
                    auto result = a.AnalyzeExpression(this, ret->RetExpr, register_local_destructor);
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
                    if (ReturnType->IsComplexType(a)) {
                        std::vector<ConcreteExpression> args;
                        args.push_back(result);
                        auto retexpr = ReturnType->BuildInplaceConstruction(a.gen->CreateParameterExpression(0), std::move(args), c);
                        return a.gen->CreateReturn(a.gen->CreateChainExpression(a.gen->CreateChainStatement(retexpr, scope->GetCleanupAllExpression(*c, ret->location)), retexpr));
                    } else {
                        auto retval = result.t->BuildValue(result, c).Expr;
                        return a.gen->CreateReturn(a.gen->CreateChainExpression(a.gen->CreateChainStatement(retval, scope->GetCleanupAllExpression(*c, ret->location)), retval));
                    }
                }
            }
            if (auto var = dynamic_cast<const AST::Variable*>(stmt)) {
                auto result = a.AnalyzeExpression(this, var->initializer, register_local_destructor);
                
                for (auto name : var->name) {
                    if (auto expr = scope->LookupName(name))
                        throw std::runtime_error("Error: variable shadowing " + name);
                }

                auto handle_variable_expression = [var, register_local_destructor, &a](ConcreteExpression result) {
                    std::vector<ConcreteExpression> args;
                    args.push_back(result);
                    Context c(a, var->location, register_local_destructor);
                    if (result.t->IsReference()) {
                        // If it's a function call, and it returns a complex T, then the return expression already points to
                        // a memory variable of type T. Just use that.
                        if (!result.steal) {
                            return result.t->Decay()->BuildLvalueConstruction(args, c);
                        } else {
                            result.steal = false;
                            if (result.t == a.GetRvalueType(result.t->Decay()))
                                result.t = a.GetLvalueType(result.t->Decay());
                            return result;
                        }
                    } else {
                        return result.t->BuildLvalueConstruction(args, c);
                    }
                };

                if (var->name.size() == 1) {
                    auto expr = handle_variable_expression(result);
                    scope->named_variables.insert(std::make_pair(var->name.front(), expr));
                    return expr.Expr;
                }
                auto tupty = dynamic_cast<TupleType*>(result.t->Decay());
                // Each name refers to each member in order.
                auto members = tupty->GetMembers();
                if (members.size() != var->name.size())
                    throw std::runtime_error("Attempted to unpack a tuple, but the number of elements was wrong.");

                // The number of elements has gotta be >0 for this to parse so don't need a nop.
                Codegen::Statement* stmt = nullptr;
                for (unsigned index = 0; index < members.size(); ++index) {
                    auto expr = handle_variable_expression(tupty->PrimitiveAccessMember(result, index, a));
                    scope->named_variables.insert(std::make_pair(var->name[index], expr));
                    stmt = stmt ? a.gen->CreateChainExpression(stmt, expr.Expr) : expr.Expr;
                }
                return stmt;
            }
        
            if (auto comp = dynamic_cast<const AST::CompoundStatement*>(stmt)) {
                LocalScope ls(scope, this);
                Context c(a, comp->location, [](ConcreteExpression e) {});
                Codegen::Statement* chain = a.gen->CreateNop();
                for (auto&& x : comp->stmts) {
                    chain = a.gen->CreateChainStatement(chain, AnalyzeStatement(x, current_scope));
                }
                return a.gen->CreateChainStatement(chain, ls->GetCleanupExpression(a, comp->location));
            }
        
            if (auto if_stmt = dynamic_cast<const AST::If*>(stmt)) {
                LocalScope condscope(scope, this);
                Context c(a, if_stmt->condition ? if_stmt->condition->location : if_stmt->var_condition->location, register_local_destructor);
                ConcreteExpression cond = if_stmt->var_condition
                    ? AnalyzeStatement(if_stmt->var_condition, condscope.get()), condscope->named_variables.begin()->second
                    : a.AnalyzeExpression(this, if_stmt->condition, register_local_destructor);
                auto expr = cond.BuildBooleanConversion(c);
                LocalScope true_scope(condscope.get(), this);
                auto true_br = AnalyzeStatement(if_stmt->true_statement, true_scope.get());
                true_br = a.gen->CreateChainStatement(true_br, true_scope->GetCleanupExpression(a, if_stmt->true_statement->location));
                Codegen::Statement* false_br = nullptr;
                if (if_stmt->false_statement) {
                    LocalScope false_scope(condscope.get(), this);
                    false_br = AnalyzeStatement(if_stmt->false_statement, false_scope.get());
                    false_br = a.gen->CreateChainStatement(false_br, false_scope->GetCleanupExpression(a, if_stmt->false_statement->location));
                }
                return a.gen->CreateChainStatement(a.gen->CreateIfStatement(expr, true_br, false_br), condscope->GetCleanupExpression(a, c.where));
            }
        
            if (auto while_stmt = dynamic_cast<const AST::While*>(stmt)) {
                Context c(a, while_stmt->condition ? while_stmt->condition->location : while_stmt->var_condition->location, register_local_destructor);
                LocalScope condscope(scope, this);
                ConcreteExpression cond = while_stmt->var_condition
                    ? AnalyzeStatement(while_stmt->var_condition, condscope.get()), condscope->named_variables.begin()->second
                    : a.AnalyzeExpression(this, while_stmt->condition, register_local_destructor);
                auto ret = condscope->current_while = a.gen->CreateWhile(cond.BuildBooleanConversion(c));
                LocalScope bodyscope(condscope.get(), this);
                auto body = AnalyzeStatement(while_stmt->body, bodyscope.get());
                body = a.gen->CreateChainStatement(body, bodyscope->GetCleanupExpression(a, while_stmt->body->location));
                body = a.gen->CreateChainStatement(body, condscope->GetCleanupExpression(a, c.where));
                condscope->current_while = nullptr;
                ret->SetBody(body);
                return a.gen->CreateChainStatement(ret, condscope->GetCleanupExpression(a, c.where));
            }

            if (auto break_stmt = dynamic_cast<const AST::Break*>(stmt)) {
                Codegen::Statement* des = a.gen->CreateNop();
                auto currscope = scope;
                Context c(a, break_stmt->location, [](ConcreteExpression e) {});
                while (currscope) {
                    if (currscope->current_while)
                        return a.gen->CreateChainStatement(des, a.gen->CreateBreak(currscope->current_while));
                    des = a.gen->CreateChainStatement(des, currscope->GetCleanupExpression(a, break_stmt->location));
                    currscope = currscope->parent;
                }
                throw std::runtime_error("Used a break but could not find a control flow statement to break out of.");
            }

            if (auto continue_stmt = dynamic_cast<const AST::Continue*>(stmt)) {
                Codegen::Statement* des = a.gen->CreateNop();
                auto currscope = scope;
                Context c(a, continue_stmt->location, [](ConcreteExpression e) {});
                while (currscope) {
                    des = a.gen->CreateChainStatement(des, currscope->GetCleanupExpression(a, continue_stmt->location));
                    if (currscope->current_while)
                        return a.gen->CreateChainStatement(des, a.gen->CreateContinue(currscope->current_while));
                    currscope = currscope->parent;
                }
                throw std::runtime_error("Used a continue but could not find a control flow statement to break out of.");
            }
        
            throw std::runtime_error("Unsupported statement.");
        };
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            exprs.push_back(AnalyzeStatement(fun->statements[i], base_scope.get()));
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
        exprs.push_back(current_scope->GetCleanupAllExpression(a, fun->where()));
        exprs.push_back(a.gen->CreateReturn()); 
    }
    s = State::AnalyzeCompleted;
    if (!codefun)
        codefun = a.gen->CreateFunction(GetSignature(a)->GetLLVMType(a), name, this);
    for (auto&& x : exprs)
        codefun->AddStatement(x);
    if (trampoline) {
        auto tramp = a.gen->CreateFunction(GetSignature(a)->GetLLVMType(a), *trampoline, this, true);
        // Args includes this if necessary, but add 1 if complex return
        std::size_t argcount = Args.size();
        if (ReturnType->IsComplexType(a))
            ++argcount;
        std::vector<Codegen::Expression*> args;
        for (std::size_t i = 0; i < argcount; ++i)
            args.push_back(a.gen->CreateParameterExpression(i));
        tramp->AddStatement(a.gen->CreateReturn(a.gen->CreateFunctionCall(a.gen->CreateFunctionValue(GetName()), args)));
    }
}
ConcreteExpression Function::BuildCall(ConcreteExpression ex, std::vector<ConcreteExpression> args, Context c) {
    if (s == State::AnalyzeCompleted) {
        ConcreteExpression val(this, c->gen->CreateFunctionValue(name));
        return GetSignature(*c)->BuildCall(val, std::move(args), c);
    } 
    if (s == State::NotYetAnalyzed) {
        ComputeBody(*c);
        return BuildCall(ex, std::move(args), c);
    }
    throw std::runtime_error("Attempted to call a function, but the analysis was not yet completed. This means that you used recursion that the type system cannot handle.");
}
Wide::Util::optional<ConcreteExpression> Function::LookupLocal(std::string name, Context c) {
    Analyzer& a = *c;
    if (name == "this" && (dynamic_cast<UserDefinedType*>(context->Decay()) || dynamic_cast<LambdaType*>(context->Decay())))
        return ConcreteExpression(c->GetLvalueType(context->Decay()), c->gen->CreateParameterExpression([this, &a] { return ReturnType->IsComplexType(a); }));
    return current_scope->LookupName(name);
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
    return current_scope->LookupName(name);
}

Type* Function::GetConstantContext(Analyzer& a) {
    struct FunctionConstantContext : public MetaType {
        Type* context;
        std::unordered_map<std::string, ConcreteExpression> constant;
        Type* GetContext(Analyzer& a) override final {
            return context;
        }
        Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression e, std::string member, Context c) override final {
            if (constant.find(member) == constant.end())
                return Wide::Util::none;
            return constant.at(member).t->Decay()->GetConstantContext(*c)->BuildValueConstruction(c);
        }
    };
    auto context = a.arena.Allocate<FunctionConstantContext>();
    context->context = GetContext(a);
    for(auto scope = current_scope; scope; scope = scope->parent) {
        for (auto&& var : scope->named_variables) {
            if (context->constant.find(var.first) == context->constant.end()) {
                auto& e = var.second;
                if (e.t->Decay()->GetConstantContext(a))
                    context->constant.insert(std::make_pair(var.first, e));
             }
        }
    }
    return context;
}