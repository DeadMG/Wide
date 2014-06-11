#include <Wide/Semantic/Function.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/ClangType.h>
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
#include <clang/AST/DeclCXX.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Function::LocalVariable::LocalVariable(Expression* ex, Function* self, Lexer::Range where)
: init_expr(std::move(ex)), self(self), where(where)
{
    ListenToNode(init_expr);
    OnNodeChanged(init_expr, Change::Contents);
}
Function::LocalVariable::LocalVariable(Expression* ex, unsigned u, Function* self, Lexer::Range where)
    : init_expr(std::move(ex)), tuple_num(u), self(self), where(where)
{
    ListenToNode(init_expr);
    OnNodeChanged(init_expr, Change::Contents);
}
void Function::LocalVariable::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (init_expr->GetType()) {
        // If we're a value we handle it at codegen time.
        auto newty = InferTypeFromExpression(init_expr->GetImplementation(), true);
        if (tuple_num) {
            if (auto tupty = dynamic_cast<TupleType*>(newty)) {
                auto tuple_access = tupty->PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(init_expr), *tuple_num);
                newty = tuple_access->GetType()->Decay();
                variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(newty, Context{ self, where });
                construction = newty->BuildInplaceConstruction(Wide::Memory::MakeUnique<ExpressionReference>(variable.get()), Expressions(std::move(tuple_access)), { self, where });
                if (newty != var_type) {
                    var_type = newty;
                    OnChange();
                }
                return;
            }
            throw std::runtime_error("fuck");
        }
        if (!init_expr->GetType()->IsReference() && init_expr->GetType() == newty) {
            if (init_expr->GetType() != var_type) {
                var_type = newty;
                OnChange();
                return;
            }
        }
        if (newty->IsReference()) {
            var_type = newty;
            OnChange();
            return;
        }
        if (newty != var_type) {
            if (newty) {
                variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(newty, Context{ self, where });
                construction = newty->BuildInplaceConstruction(Wide::Memory::MakeUnique<ExpressionReference>(variable.get()), Expressions(Wide::Memory::MakeUnique<ExpressionReference>(init_expr)), { self, where });
            }
            var_type = newty;
            OnChange();
        }
        return;
    }
    if (var_type) {
        var_type = nullptr;
        OnChange();
    }
}
llvm::Value* Function::LocalVariable::ComputeValue(CodegenContext& con) {
    if (init_expr->GetType() == var_type) {
        // If they return a complex value by value, just steal it, and don't worry about destructors as it will already have handled it.
        if (init_expr->GetType()->IsComplexType(con) || var_type->IsReference()) return init_expr->GetValue(con);
        // If they return a simple by value, then we can just alloca it fine, no destruction needed.
        auto alloc = con.alloca_builder->CreateAlloca(var_type->GetLLVMType(con));
        alloc->setAlignment(var_type->alignment());
        con->CreateStore(init_expr->GetValue(con), alloc);
        return alloc;
    }
   
    construction->GetValue(con);
    if (var_type->IsComplexType(con))
        con.Destructors.push_back(variable.get());
    return variable->GetValue(con);
}
Type* Function::LocalVariable::GetType() {
    if (var_type) {
        if (var_type->IsReference())
            return self->analyzer.GetLvalueType(var_type->Decay());
        return self->analyzer.GetLvalueType(var_type);
    }
    return nullptr;
}

Function::Scope::Scope(Scope* s) : parent(s), current_while(nullptr) {
    if (parent)
        parent->children.push_back(std::unique_ptr<Scope>(this));
}

Function::WhileStatement* Function::Scope::GetCurrentWhile() {
    if (current_while)
        return current_while;
    if (parent)
        return parent->GetCurrentWhile();
    return nullptr;
}

Function::CompoundStatement::CompoundStatement(Scope* s)
    : s(s) {}
void Function::CompoundStatement::GenerateCode(CodegenContext& con) {
    con.GenerateCodeAndDestroyLocals([this](CodegenContext& nested) {
        for (auto&& stmt : s->active)
            stmt->GenerateCode(nested);
    });
}

Function::ReturnStatement::ReturnStatement(Function* f, std::unique_ptr<Expression> expr, Scope* current, Lexer::Range where)
: self(f), ret_expr(std::move(expr)), where(where)
{
    self->returns.insert(this);
    if (ret_expr) {
        ListenToNode(ret_expr.get());
    }
    OnNodeChanged(ret_expr.get(), Change::Contents);
    ListenToNode(self);
}
void Function::ReturnStatement::OnNodeChanged(Node* n, Change c) {
    if (self->ReturnType != self->analyzer.GetVoidType()) {
        struct ReturnEmplaceValue : Expression {
            ReturnEmplaceValue(Function* f)
            : self(f) {}
            Function* self;
            Type* GetType() override final {
                return self->analyzer.GetLvalueType(self->ReturnType);
            }
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                return self->llvmfunc->arg_begin();
            }
        };
        if (self->ReturnType)
            build = self->ReturnType->BuildInplaceConstruction(Wide::Memory::MakeUnique<ReturnEmplaceValue>(self), Expressions(Wide::Memory::MakeUnique<ExpressionReference>(ret_expr.get())), { self, where });
        else
            build = nullptr;
    } else
        build = nullptr;
}
void Function::ReturnStatement::GenerateCode(CodegenContext& con) {
    // Consider the simple cases first.
    // If our return type is void
    if (self->ReturnType == self->analyzer.GetVoidType()) {
        // If we have a void-returning expression, evaluate it, destroy it, then return.
        if (ret_expr) {
            ret_expr->GetValue(con);
        }
        con.DestroyAll();
        con->CreateRetVoid();
        return;
    }

    // If we return a simple type
    if (!self->ReturnType->IsComplexType(con)) {
        // and we already have an expression of that type
        if (ret_expr->GetType() == self->ReturnType) {
            // then great, just return it directly.
            auto val = ret_expr->GetValue(con);
            con.DestroyAll();
            con->CreateRet(val);
            return;
        }
        // If we have a reference to it, just load it right away.
        if (ret_expr->GetType()->IsReference(self->ReturnType)) {
            auto val = con->CreateLoad(ret_expr->GetValue(con));
            con.DestroyAll();
            con->CreateRet(val);
            return;
        }
        // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
        // We would fix this up, but, cannot query the complexity of a type prior to code generation.
        build = self->ReturnType->BuildValueConstruction(Expressions(Wide::Memory::MakeUnique<ExpressionReference>(ret_expr.get())), { self, where });
        auto val = build->GetValue(con);
        con.DestroyAll();
        con->CreateRet(val);
        return;
    }

    // If we return a complex type, the 0th parameter will be memory into which to place the return value.
    // build should be a function taking the memory and our ret's value and emplacing it.
    // Then return void.
    build->GetValue(con);
    con.DestroyAll();
    con->CreateRetVoid();
    return;
}


bool is_terminated(llvm::BasicBlock* bb) {
    return !bb->empty() && bb->back().isTerminator();
}

Function::WhileStatement::WhileStatement(Expression* ex, Lexer::Range where, Function* s)
: cond(std::move(ex)), where(where), self(s)
{
    ListenToNode(cond);
    OnNodeChanged(cond, Change::Contents);
}
void Function::WhileStatement::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (cond->GetType())
        boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(Wide::Memory::MakeUnique<ExpressionReference>(cond), { self, where });
}

void Function::WhileStatement::GenerateCode(CodegenContext& con) {
    source_con = &con;
    check_bb = llvm::BasicBlock::Create(con, "check_bb", con->GetInsertBlock()->getParent());
    auto loop_bb = llvm::BasicBlock::Create(con, "loop_bb", con->GetInsertBlock()->getParent());
    continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
    con->CreateBr(check_bb);
    con->SetInsertPoint(check_bb);
    // Both of these branches need to destroy the cond's locals.
    // In the case of the loop, so that when check_bb is re-entered, it's clear.
    // In the case of the continue, so that the check's locals are cleaned up properly.
    CodegenContext condition_context(con);
    condition_con = &condition_context;
    auto condition = boolconvert->GetValue(condition_context);
    if (condition->getType() == llvm::Type::getInt8Ty(condition_context))
        condition = condition_context->CreateTrunc(condition, llvm::Type::getInt1Ty(condition_context));
    condition_context->CreateCondBr(condition, loop_bb, continue_bb);
    condition_context->SetInsertPoint(loop_bb);
    condition_context.GenerateCodeAndDestroyLocals([this](CodegenContext& body_context) {
        body->GenerateCode(body_context);
    });
    con.DestroyDifference(condition_context);
    // If, for example, we unconditionally return or break/continue, it can happen that we were already terminated.
    if (!is_terminated(con->GetInsertBlock()))
        con->CreateBr(check_bb);
    con->SetInsertPoint(continue_bb);
    con.DestroyDifference(condition_context);
    condition_con = nullptr;
    source_con = nullptr;
}

Function::IfStatement::IfStatement(Expression* cond, Statement* true_b, Statement* false_b, Lexer::Range where, Function* s)
: cond(std::move(cond)), true_br(std::move(true_b)), false_br(std::move(false_b)), where(where), self(s)
{
    ListenToNode(cond);
    OnNodeChanged(cond, Change::Contents);
}
void Function::IfStatement::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (cond->GetType())
        boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(Wide::Memory::MakeUnique<ExpressionReference>(cond), { self, where });
}
void Function::IfStatement::GenerateCode(CodegenContext& con) {
    auto true_bb = llvm::BasicBlock::Create(con, "true_bb", con->GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
    auto else_bb = false_br ? llvm::BasicBlock::Create(con->getContext(), "false_bb", con->GetInsertBlock()->getParent()) : continue_bb;
    con.GenerateCodeAndDestroyLocals([this, true_bb, continue_bb, else_bb](CodegenContext& condition_con) {
        auto condition = boolconvert->GetValue(condition_con);
        if (condition->getType() == llvm::Type::getInt8Ty(condition_con))
            condition = condition_con->CreateTrunc(condition, llvm::Type::getInt1Ty(condition_con));
        condition_con->CreateCondBr(condition, true_bb, else_bb);
        condition_con->SetInsertPoint(true_bb);
        condition_con.GenerateCodeAndDestroyLocals([this, continue_bb](CodegenContext& true_con) {
            true_br->GenerateCode(true_con);
            if (!is_terminated(true_con->GetInsertBlock()))
                true_con->CreateBr(continue_bb);
        });
        if (false_br) {
            condition_con->SetInsertPoint(else_bb);
            condition_con.GenerateCodeAndDestroyLocals([this, continue_bb](CodegenContext& false_con) {
                false_br->GenerateCode(false_con);
                false_con.DestroyDifference(false_con);
                if (!is_terminated(false_con->GetInsertBlock()))
                    false_con->CreateBr(continue_bb);
            });
        }
        condition_con->SetInsertPoint(continue_bb);
    });
}

Function::VariableStatement::VariableStatement(std::vector<LocalVariable*> locs, std::unique_ptr<Expression> expr)
: locals(std::move(locs)), init_expr(std::move(expr)){}
void Function::VariableStatement::GenerateCode(CodegenContext& con) {
    init_expr->GetValue(con);
    for (auto local : locals)
        local->GetValue(con);
}

std::unique_ptr<Expression> Function::Scope::LookupLocal(std::string name) {
    if (named_variables.find(name) != named_variables.end()) {
        auto&& ref = named_variables.at(name);
        auto&& first = ref.first;
        return Wide::Memory::MakeUnique<ExpressionReference>(first.get());
    }
    if (parent)
        return parent->LookupLocal(name);
    return nullptr;
}
struct Function::LocalScope {
    LocalScope(Function* f) 
    : func(f) {
        previous = f->current_scope;
        f->current_scope = s = new Scope(previous);
    }
    Function* func;
    Scope* s;
    Scope* operator->() { return s; }
    Scope* previous;
    ~LocalScope() {
        func->current_scope = previous;
    }
};
Function::BreakStatement::BreakStatement(Scope* s) {
    // The continue_bb already has code to destroy the condition's locals.
    // So don't destroy them again here.
    // Just destroy the body's locals.
    while_stmt = s->GetCurrentWhile();
}
void Function::BreakStatement::GenerateCode(CodegenContext& con) {
    while_stmt->condition_con->DestroyDifference(con);
    con->CreateBr(while_stmt->continue_bb);
}
Function::ContinueStatement::ContinueStatement(Scope* s) {
    // The check_bb is going to expect that the cond's memory is clear for use.
    // So clean them up before we can re-create in that space.
    while_stmt = s->GetCurrentWhile();
}
void Function::ContinueStatement::GenerateCode(CodegenContext& con) {
    while_stmt->source_con->DestroyDifference(con);
    con->CreateBr(while_stmt->check_bb);
}
Function::ThrowStatement::ThrowStatement(std::unique_ptr<Expression> expr, Context c) {
    // http://mentorembedded.github.io/cxx-abi/abi-eh.html
    // 2.4.2
    struct ExceptionAllocateMemory : Expression {
        ExceptionAllocateMemory(Type* t)
        : alloc(t) {
            auto&& analyzer = t->analyzer;
            if (t->alignment() > std::max(analyzer.GetDataLayout().getABIIntegerTypeAlignment(64), analyzer.GetDataLayout().getPointerABIAlignment()))
                throw std::runtime_error("EH runtime does not provide memory of enough alignment to support this exception type.");
        }
        Type* alloc;
        Type* GetType() override final { return alloc->analyzer.GetLvalueType(alloc); }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto size_t_ty = llvm::IntegerType::get(con, alloc->analyzer.GetDataLayout().getPointerSizeInBits());
            auto allocate_exception = con.module->getFunction("__cxa_allocate_exception");
            if (!allocate_exception) {
                llvm::Type* args[] = { size_t_ty };
                auto fty = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(con), args, false);
                allocate_exception = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_allocate_exception", con.module);
            }
            return con->CreatePointerCast(con->CreateCall(allocate_exception, { llvm::ConstantInt::get(size_t_ty, alloc->size(), false) }), GetType()->GetLLVMType(con));
        }
    };
    ty = expr->GetType()->Decay();
    auto except_memory = Wide::Memory::MakeUnique<ExceptionAllocateMemory>(ty);
    exception = ty->BuildInplaceConstruction(std::move(except_memory), Expressions(std::move(expr)), c);
}
void Function::ThrowStatement::GenerateCode(CodegenContext& con) {
    auto value = exception->GetValue(con);
    // Throw this shit.
    auto cxa_throw = con.module->getFunction("__cxa_throw");
    if (!cxa_throw) {
        llvm::Type* args[] = { con.GetInt8PtrTy(), con.GetInt8PtrTy(), llvm::FunctionType::get(llvm::Type::getVoidTy(con), { con.GetInt8PtrTy() }, false)->getPointerTo() };
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(con), args, false);
        cxa_throw = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_throw", con);
    }
    llvm::Value* args[] = { con->CreatePointerCast(value, con.GetInt8PtrTy()), ty->GetRTTI(con), ty->GetDestructorFunction(con) };
    con->CreateCall(cxa_throw, args);
}
std::unique_ptr<Statement> Function::AnalyzeStatement(const AST::Statement* s) {
    if (auto ret = dynamic_cast<const AST::Return*>(s)) {
        if (ret->RetExpr) {
            return Wide::Memory::MakeUnique<ReturnStatement>(this, analyzer.AnalyzeExpression(this, ret->RetExpr), current_scope, ret->location);
        }
        return Wide::Memory::MakeUnique<ReturnStatement>(this, nullptr, current_scope, ret->location);
    }

    if (auto comp = dynamic_cast<const AST::CompoundStatement*>(s)) {
        LocalScope compound(this);
        for (auto stmt : comp->stmts)
            compound->active.push_back(AnalyzeStatement(stmt));
        return Wide::Memory::MakeUnique<CompoundStatement>(compound.s);
    }

    if (auto expr = dynamic_cast<const AST::Expression*>(s))
        return analyzer.AnalyzeExpression(this, expr);

    if (auto var = dynamic_cast<const AST::Variable*>(s)) {
        std::vector<LocalVariable*> locals;
        auto init_expr = analyzer.AnalyzeExpression(this, var->initializer);
        if (var->name.size() == 1) {
            auto&& name = var->name.front();
            if (current_scope->named_variables.find(var->name.front().name) != current_scope->named_variables.end())
                throw VariableShadowing(name.name, current_scope->named_variables.at(name.name).second, name.where);
            auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr.get(), this, var->name.front().where);
            locals.push_back(var_stmt.get());
            current_scope->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
        } else {
            unsigned i = 0;
            for (auto&& name : var->name) {
                if (current_scope->named_variables.find(name.name) != current_scope->named_variables.end())
                    throw VariableShadowing(name.name, current_scope->named_variables.at(name.name).second, name.where);
                auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr.get(), i++, this, name.where);
                locals.push_back(var_stmt.get());
                current_scope->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
            }
        }
        return Wide::Memory::MakeUnique<VariableStatement>(std::move(locals), std::move(init_expr));
    }

    if (auto whil = dynamic_cast<const AST::While*>(s)) {
        LocalScope condscope(this);
        auto get_expr = [&, this]() -> std::unique_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(whil->var_condition));
                return Wide::Memory::MakeUnique<ExpressionReference>(condscope->named_variables.begin()->second.first.get());
            }
            return analyzer.AnalyzeExpression(this, whil->condition);
        };
        auto cond = get_expr();
        auto while_stmt = Wide::Memory::MakeUnique<WhileStatement>(cond.get(), whil->location, this);
        condscope->active.push_back(std::move(cond));
        condscope->current_while = while_stmt.get();
        LocalScope bodyscope(this);
        bodyscope->active.push_back(AnalyzeStatement(whil->body));
        while_stmt->body = bodyscope->active.back().get();
        return std::move(while_stmt);
    }

    if (auto break_stmt = dynamic_cast<const AST::Break*>(s)) {
        if (!current_scope->GetCurrentWhile())
            throw NoControlFlowStatement(break_stmt->location);
        return Wide::Memory::MakeUnique<BreakStatement>(current_scope);
    }
    if (auto continue_stmt = dynamic_cast<const AST::Continue*>(s)) {
        if (!current_scope->GetCurrentWhile())
            throw NoControlFlowStatement(continue_stmt->location);
        return Wide::Memory::MakeUnique<ContinueStatement>(current_scope);
    }

    if (auto if_stmt = dynamic_cast<const AST::If*>(s)) {
        LocalScope condscope(this);
        auto get_expr = [&, this]() -> std::unique_ptr<Expression> {
            if (if_stmt->var_condition) {
                if (if_stmt->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(if_stmt->var_condition));
                return Wide::Memory::MakeUnique<ExpressionReference>(condscope->named_variables.begin()->second.first.get());
            }
            return analyzer.AnalyzeExpression(this, if_stmt->condition);
        };
        auto cond = get_expr();
        Expression* condexpr = cond.get();
        condscope->active.push_back(std::move(cond));
        Statement* true_br = nullptr;
        {
            LocalScope truescope(this);
            truescope->active.push_back(AnalyzeStatement(if_stmt->true_statement));
            true_br = truescope->active.back().get();
        }
        Statement* false_br = nullptr;
        if (if_stmt->false_statement) {
            LocalScope falsescope(this);
            falsescope->active.push_back(AnalyzeStatement(if_stmt->false_statement));
            false_br = falsescope->active.back().get();
        }
        return Wide::Memory::MakeUnique<IfStatement>(condexpr, true_br, false_br, if_stmt->location, this);
    }

    if (auto thro = dynamic_cast<const Wide::AST::Throw*>(s)) {
        auto expression = analyzer.AnalyzeExpression(this, thro->expr);
        return Wide::Memory::MakeUnique<ThrowStatement>(std::move(expression), Context(this, thro->location));
    }
    assert(false && "Unsupported statement.");
}

Function::Parameter::Parameter(Function* s, unsigned n, Lexer::Range where)
: self(s), num(n), where(where)
{
    OnNodeChanged(s, Change::Contents);
    ListenToNode(self);
}
void Function::Parameter::OnNodeChanged(Node* n, Change what) {
    auto get_new_ty = [this]() -> Type* {
        auto root_ty = self->Args[num];
        if (root_ty->IsReference())
            return self->analyzer.GetLvalueType(root_ty->Decay()); // Is this wrong in the case of named rvalue reference?
        return self->analyzer.GetLvalueType(root_ty);
    };
    auto new_ty = get_new_ty();
    if (new_ty != cur_ty) {
        cur_ty = new_ty;
        destructor = cur_ty->BuildDestructorCall(Wide::Memory::MakeUnique<ExpressionReference>(this), { self, where });
        OnChange();
    }
}
Type* Function::Parameter::GetType() {
    return cur_ty;
}
void Function::Parameter::DestroyExpressionLocals(CodegenContext& con) {
    if (self->Args[num]->IsComplexType(con))
        destructor->GetValue(con);
}
llvm::Value* Function::Parameter::ComputeValue(CodegenContext& con) {
    auto argnum = num;
    if (self->ReturnType->IsComplexType(con))
        ++argnum;
    auto llvm_argument = std::next(self->llvmfunc->arg_begin(), argnum);

    if (self->Args[num]->IsComplexType(con))
        con.Destructors.push_back(this);

    if (self->Args[num]->IsComplexType(con) || self->Args[num]->IsReference())
        return llvm_argument;

    auto alloc = con.alloca_builder->CreateAlloca(self->Args[num]->GetLLVMType(con));
    alloc->setAlignment(self->Args[num]->alignment());
    con->CreateStore(llvm_argument, alloc);
    return alloc;
}

Function::Function(std::vector<Type*> args, const AST::FunctionBase* astfun, Analyzer& a, Type* mem, std::string src_name)
: MetaType(a)
, ReturnType(nullptr)
, fun(astfun)
, context(mem)
, s(State::NotYetAnalyzed)
, root_scope(nullptr)
, source_name(src_name) {
    // Only match the non-concrete arguments.
    root_scope = Wide::Memory::MakeUnique<Scope>(nullptr);
    current_scope = root_scope.get();
    unsigned num = 0;
    // We might still be a member function if we're exported as one later.
    if (mem && (dynamic_cast<MemberFunctionContext*>(mem->Decay()))) {
        Args.push_back(args[0] == args[0]->Decay() ? a.GetLvalueType(args[0]) : args[0]);
        NonstaticMemberContext = args[0];
        if (auto con = dynamic_cast<Semantic::ConstructorContext*>(args[0]->Decay()))
            ConstructorContext = con;
        args.erase(args.begin());
        auto param = Wide::Memory::MakeUnique<Parameter>(this, num++, Lexer::Range(nullptr));
        root_scope->named_variables.insert(std::make_pair("this", std::make_pair(Wide::Memory::MakeUnique<ExpressionReference>(param.get()), Lexer::Range(nullptr))));
        root_scope->active.push_back(std::move(param));
    }
    Args.insert(Args.end(), args.begin(), args.end());
    for(auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
        auto param = Wide::Memory::MakeUnique<Parameter>(this, num++, arg.location);
        root_scope->named_variables.insert(std::make_pair(arg.name, std::make_pair(Wide::Memory::MakeUnique<ExpressionReference>(param.get()), arg.location)));
        root_scope->active.push_back(std::move(param));
    }

    std::stringstream strstr;
    strstr << "__" << std::hex << this;
    name = strstr.str();

    // Deal with the prolog first- if we have one.
    if (auto fun = dynamic_cast<const AST::Function*>(astfun)) {
        for (auto&& prolog : fun->prolog) {
            auto ass = dynamic_cast<const AST::BinaryExpression*>(prolog);
            if (!ass || ass->type != Lexer::TokenType::Assignment)
                throw PrologNonAssignment(prolog->location);
            auto ident = dynamic_cast<const AST::Identifier*>(ass->lhs);
            if (!ident)
                throw PrologAssignmentNotIdentifier(ass->lhs->location);
            auto expr = analyzer.AnalyzeExpression(this, ass->rhs);
            if (ident->val == "ExportName") {
                auto str = dynamic_cast<StringType*>(expr->GetType()->Decay());
                if (!str)
                    throw PrologExportNotAString(ass->rhs->location);
                trampoline.push_back([str](llvm::Module* module) { return str->GetValue(); });
            }
            if (ident->val == "ReturnType") {
                auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay());
                if (!ty)
                    throw NotAType(expr->GetType()->Decay(), ass->rhs->location);
                ExplicitReturnType = ty->GetConstructedType();
                ReturnType = *ExplicitReturnType;
            }
            if (ident->val == "ExportAs") {
                auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                if (!overset)
                    throw NotAType(expr->GetType()->Decay(), ass->rhs->location);
                auto tuanddecl = overset->GetSingleFunction();
                if (!tuanddecl.second) throw NotAType(expr->GetType()->Decay(), ass->rhs->location);
                auto tu = tuanddecl.first;
                auto decl = tuanddecl.second;
                trampoline.push_back(tu->MangleName(decl));
                if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                    ClangContexts.insert(analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent())));
                    if (!meth->isStatic()) {
                        // Add "this".
                        auto param = Wide::Memory::MakeUnique<Parameter>(this, 0, Lexer::Range(nullptr));
                        root_scope->named_variables.insert(std::make_pair("this", std::make_pair(Wide::Memory::MakeUnique<ExpressionReference>(param.get()), Lexer::Range(nullptr))));
                        root_scope->active.push_back(std::move(param));
                        auto clangty = dynamic_cast<ClangType*>(analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent())));
                        if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(meth)) {
                            // Error if we have a constructor context and it's not the same one.
                            // Error if we have a nonstatic member context.
                            // Error if we have any other clang contexts, since constructors cannot be random static members.
                            if (NonstaticMemberContext) throw std::runtime_error("fuck");
                            if (ConstructorContext && *ConstructorContext != clangty) throw std::runtime_error("fuck");
                            if (ClangContexts.size() >= 2) throw std::runtime_error("fuck");
                            ConstructorContext = clangty;
                            continue;
                        }
                        // Error if we have a constructor context.
                        // Error if we have a nonstatic member context and it's not this one.
                        if (ConstructorContext) throw std::runtime_error("fuck");
                        if (NonstaticMemberContext && *NonstaticMemberContext != clangty) throw std::runtime_error("fuck");
                        NonstaticMemberContext = clangty;
                        continue;
                    }
                }
            }
        }
    }
}

Wide::Util::optional<clang::QualType> Function::GetClangType(ClangTU& where) {
    return GetSignature()->GetClangType(where);
}

void Function::ComputeBody() {
    if (s == State::NotYetAnalyzed) {
        s = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor
        if (auto con = dynamic_cast<const AST::Constructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            for (auto&& x : members) {
                auto has_initializer = [&](std::string name) -> const AST::Variable* {
                    for (auto&& x : con->initializers) {
                        // Can only have 1 name- AST restriction
                        assert(x->name.size() == 1);
                        if (x->name.front().name == name)
                            return x;
                    }
                    return nullptr;
                };
                auto num = x.num;
                auto result = analyzer.GetLvalueType(x.t);
                // Gotta get the correct this pointer.
                auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, num.offset);
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
                if (auto init = has_initializer(x.name)) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    
                    if (init->initializer)
                        root_scope->active.push_back(x.t->BuildInplaceConstruction(std::move(member), Expressions(analyzer.AnalyzeExpression(this, init->initializer)), { this, init->location }));
                    else
                        root_scope->active.push_back(x.t->BuildInplaceConstruction(std::move(member), Expressions(), { this, init->location }));
                    continue;
                }
                // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                if (x.InClassInitializer) {
                    root_scope->active.push_back(x.t->BuildInplaceConstruction(std::move(member), Expressions(x.InClassInitializer(LookupLocal("this"))), { this, x.location }));
                    continue;
                }
                root_scope->active.push_back(x.t->BuildInplaceConstruction(std::move(member), Expressions(), { this, fun->where }));
            }
            for (auto&& x : con->initializers) {
                if (std::find_if(members.begin(), members.end(), [&](decltype(*members.begin())& ref) { return ref.name == x->name.front().name; }) == members.end())
                    throw NoMemberToInitialize(context->Decay(), x->name.front().name, x->location);
            }
            // set the vptrs if necessary
            auto ty = dynamic_cast<Type*>(member);
            root_scope->active.push_back(ty->SetVirtualPointers(LookupLocal("this")));
        }
        // Now the body.
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            root_scope->active.push_back(AnalyzeStatement(fun->statements[i]));
        }        

        // Compute the return type.
        ComputeReturnType();
        s = State::AnalyzeCompleted;
    }
}

void Function::ComputeReturnType() {
    if (!ExplicitReturnType) {
        if (returns.size() == 0) {
            ReturnType = analyzer.GetVoidType();
            return;
        }

        std::unordered_set<Type*> ret_types;
        for (auto ret : returns) {
            if (!ret->ret_expr) {
                ret_types.insert(analyzer.GetVoidType()); 
                continue;
            }
            if (!ret->ret_expr->GetType()) continue;
            ret_types.insert(ret->ret_expr->GetType()->Decay());
        }

        if (ret_types.size() == 1) {
            ReturnType = *ret_types.begin();
            OnChange();
            return;
        }

        // If there are multiple return types, there should be a single return type where the rest all is-a that one.
        std::unordered_set<Type*> isa_rets;
        for (auto ret : ret_types) {
            auto the_rest = ret_types;
            the_rest.erase(ret);
            auto all_isa = [&] {
                for (auto other : the_rest) {
                    if (!other->IsA(other, ret, GetAccessSpecifier(this, ret)))
                        return false;
                }
                return true;
            };
            if (all_isa())
                isa_rets.insert(ret);
        }
        if (isa_rets.size() == 1) {
            ReturnType = *ret_types.begin();
            OnChange();
            return;
        }
        throw std::runtime_error("Fuck");
    }
}

void Function::EmitCode(llvm::Module* module) {
    if (llvmfunc)
        return;
    auto sig = GetSignature();
    auto llvmsig = sig->GetLLVMType(module);
    llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, name, module);

    llvm::BasicBlock* allocas = llvm::BasicBlock::Create(module->getContext(), "allocas", llvmfunc);
    llvm::IRBuilder<> allocs(allocas);
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(module->getContext(), "entry", llvmfunc);
    allocs.SetInsertPoint(allocs.CreateBr(bb));
    llvm::IRBuilder<> irbuilder(bb);
    CodegenContext c;
    c.alloca_builder = &allocs;
    c.insert_builder = &irbuilder;
    c.module = module;
    for (auto&& stmt : root_scope->active)
        stmt->GenerateCode(c);

    if (!is_terminated(irbuilder.GetInsertBlock())) {
        if (ReturnType == analyzer.GetVoidType()) {
            c.DestroyAll();
            irbuilder.CreateRetVoid();
        }
        else
            irbuilder.CreateUnreachable();
    }

    if (llvm::verifyFunction(*llvmfunc, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");

    // Keep track - we may be asked to export to the same name multiple times.
    std::unordered_set<std::string> names;
    for (auto exportnam : trampoline) {
        auto exportname = exportnam(module);
        if (names.find(exportname) != names.end())
            continue;
        names.insert(exportname);
        // oh yay.
        // Gotta handle ABI mismatch.

        // Check Clang's uses of this function - if it bitcasts them all we're good.

        auto ourret = llvmfunc->getFunctionType()->getReturnType();
        auto int8ty = llvm::IntegerType::getInt8Ty(module->getContext());
        auto int1ty = llvm::IntegerType::getInt1Ty(module->getContext());
        llvm::Type* trampret;
        llvm::Function* tramp = nullptr;
        std::vector<llvm::Value*> args;
        if (tramp = module->getFunction(exportname)) {
            // Someone- almost guaranteed Clang- already generated a function with this name.
            // First handle bitcast WTFery
            // Sometimes Clang generates functions with totally the wrong type, then bitcasts them on every use.
            // So just change the function decl to match the bitcast type.
            llvm::Type* CastType = nullptr;
            for (auto use_it = tramp->use_begin(); use_it != tramp->use_end(); ++use_it) {
                auto use = *use_it;
                if (auto cast = llvm::dyn_cast<llvm::CastInst>(use)) {
                    if (CastType) {
                        if (CastType != cast->getDestTy()) {
                            throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
                        }
                    } else {
                        if (auto ptrty = llvm::dyn_cast<llvm::PointerType>(cast->getDestTy()))
                            if (auto functy = llvm::dyn_cast<llvm::FunctionType>(ptrty->getElementType()))
                                CastType = cast->getDestTy();
                    }
                }
                if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use)) {
                    if (CastType) {
                        if (CastType != constant->getType()) {
                            throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
                        }
                    } else {
                        if (auto ptrty = llvm::dyn_cast<llvm::PointerType>(constant->getType()))
                            if (auto functy = llvm::dyn_cast<llvm::FunctionType>(ptrty->getElementType()))
                                CastType = constant->getType();
                    }
                }
            }
            // There are no uses that are invalid.
            if (CastType || std::distance(tramp->use_begin(), tramp->use_end()) == 0) {
                if (!CastType) CastType = llvmfunc->getType();
                tramp->setName("__fucking__clang__type_hacks");
                auto badf = tramp;
                auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(CastType)->getElementType());
                tramp = llvm::Function::Create(t, llvm::GlobalValue::LinkageTypes::ExternalLinkage, exportname, module);
                // Update all Clang's uses
                for (auto use_it = badf->use_begin(); use_it != badf->use_end(); ++use_it) {
                    auto use = *use_it;
                    if (auto cast = llvm::dyn_cast<llvm::CastInst>(use))
                        cast->replaceAllUsesWith(tramp);
                    if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use))
                        constant->replaceAllUsesWith(tramp);
                }
                badf->removeFromParent();
            }

            trampret = tramp->getFunctionType()->getReturnType();
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(module->getContext(), "entry", tramp);
            llvm::IRBuilder<> irbuilder(&tramp->getEntryBlock());
            if (tramp->getFunctionType() != llvmfunc->getFunctionType()) {
                // Clang's idea of the LLVM representation of this function disagrees with our own.
                // First, check the arguments.
                auto fty = tramp->getFunctionType();
                auto ty = llvmfunc->getFunctionType();
                auto arg_begin = tramp->getArgumentList().begin();
                for (std::size_t i = 0; i < ty->getNumParams(); ++i) {
                    // If this particular type is a match, then go for it.
                    if (ty->getParamType(i) == arg_begin->getType()) {
                        args.push_back(arg_begin);
                        ++arg_begin;
                        continue;
                    }
                    
                    // If it's i1 and we expected i8, just zext it.
                    if (ty->getParamType(i) == int8ty && arg_begin->getType() == int1ty) {
                        args.push_back(irbuilder.CreateZExt(arg_begin, int8ty));
                        ++arg_begin;
                        continue;
                    }

                    // Clang elides 0-sized arguments by value.
                    // Check our actual semantic argument for this, not the LLVM type, as Clang codegens them to have a size of 1.
                    if (Args[i]->IsEmpty()) {
                        args.push_back(llvm::Constant::getNullValue(ty->getParamType(i)));
                        continue;
                    }

                    throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
                }
                if (ourret != trampret) {
                    if (ourret != llvm::IntegerType::getInt8Ty(module->getContext()) || trampret != llvm::IntegerType::getInt1Ty(module->getContext()))
                        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
                }
            } else {
                // This ever happens?
                for (auto it = tramp->arg_begin(); it != tramp->arg_end(); ++it)
                    args.push_back(&*it);
            }
        } else {
            trampret = ourret;
            auto fty = llvmfunc->getFunctionType();
            if (exportname == "main")
                fty = llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(module->getContext()), false);
            tramp = llvm::Function::Create(fty, llvm::GlobalValue::ExternalLinkage, exportname, module);
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(module->getContext(), "entry", tramp);
            llvm::IRBuilder<> irbuilder(&tramp->getEntryBlock());
            for (auto it = tramp->arg_begin(); it != tramp->arg_end(); ++it)
                args.push_back(&*it);
        }

        llvm::IRBuilder<> irbuilder(&tramp->getEntryBlock());

        // May be ABI mismatch between ourselves and llvmfunc.
        // Consider that we may have to truncate the result, and we may have to add ret 0 for main.
        if (ReturnType == analyzer.GetVoidType()) {
            if (exportname == "main") {
                irbuilder.CreateCall(llvmfunc, args);
                irbuilder.CreateRet(irbuilder.getInt32(0));
            } else {
                irbuilder.CreateCall(llvmfunc, args);
                irbuilder.CreateRetVoid();
            }
        } else {
            auto call = (llvm::Value*)irbuilder.CreateCall(llvmfunc, args);
            if (ourret == int8ty && trampret == int1ty)
                call = irbuilder.CreateTrunc(call, int1ty);
            irbuilder.CreateRet(call);
        }
        if (llvm::verifyFunction(*tramp, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
    }
}

std::unique_ptr<Expression> Function::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    struct Self : public Expression {
        Self(Function* self, Expression* expr, std::unique_ptr<Expression> val)
        : self(self), val(std::move(val))
        {
            if (!expr) return;
            if (auto func = dynamic_cast<const AST::Function*>(self->fun)) {
                udt = dynamic_cast<UserDefinedType*>(expr->GetType()->Decay());
                if (!udt) return;
                obj = udt->GetVirtualPointer(Wide::Memory::MakeUnique<ExpressionReference>(expr));
            }
        }
        UserDefinedType* udt;
        std::unique_ptr<Expression> obj;
        std::unique_ptr<Expression> val;
        Function* self;
        Type* GetType() override final {
            return self->GetSignature();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            if (!self->llvmfunc)
                self->EmitCode(con);
            val->GetValue(con);
            if (obj) {
                auto func = dynamic_cast<const AST::Function*>(self->fun);
                assert(func);
                auto vindex = udt->GetVirtualFunctionIndex(func);
                if (!vindex) return self->llvmfunc;
                auto vptr = con->CreateLoad(obj->GetValue(con));
                return con->CreatePointerCast(con->CreateLoad(con->CreateConstGEP1_32(vptr, *vindex)), self->GetSignature()->GetLLVMType(con));
            }
            return self->llvmfunc;
        }
    };
    auto self = !args.empty() ? args[0].get() : nullptr;
    return GetSignature()->BuildCall(Wide::Memory::MakeUnique<Self>(this, self, std::move(val)), std::move(args), c);
}

std::unique_ptr<Expression> Function::LookupLocal(std::string name) {
    //if (name == "this" && dynamic_cast<MemberFunctionContext*>(context->Decay()))
    //    return ConcreteExpression(c->GetLvalueType(context->Decay()), c->gen->CreateParameterExpression([this, &a] { return ReturnType->IsComplexType(a); }));
    return current_scope->LookupLocal(name);
}
std::string Function::GetName() {
    return name;
}
FunctionType* Function::GetSignature() {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    if (s == State::AnalyzeInProgress)
        assert(false && "Attempted to call GetSignature whilst a function was still being analyzed.");
    assert(ReturnType);
    return analyzer.GetFunctionType(ReturnType, Args, false);
}

Type* Function::GetConstantContext() {
    return nullptr;
}
std::string Function::explain() {
    auto args = std::string("(");
    unsigned i = 0;
    for (auto& ty : Args) {
        if (Args.size() == fun->args.size() + 1 && i == 0) {
            args += "this := ";
            ++i;
        } else {
            args += fun->args[i++].name + " := ";
        }
        if (&ty != &Args.back())
            args += ty->explain() + ", ";
        else
            args += ty->explain();
    }
    args += ")";

    std::string context_name = context->explain() + "." + source_name;
    if (context == analyzer.GetGlobalModule())
        context_name = "." + source_name;
    return context_name + args + " at " + fun->where;
}
Function::~Function() {}