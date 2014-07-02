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
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangType.h>
#include <unordered_set>
#include <sstream>
#include <iostream>

#pragma warning(push, 0)
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_os_ostream.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Function::LocalVariable::LocalVariable(std::shared_ptr<Expression> ex, Function* self, Lexer::Range where, Lexer::Range init_where)
: init_expr(std::move(ex)), self(self), where(where), init_where(init_where)
{
    ListenToNode(init_expr.get());
    OnNodeChanged(init_expr.get(), Change::Contents);
}
Function::LocalVariable::LocalVariable(std::shared_ptr<Expression> ex, unsigned u, Function* self, Lexer::Range where, Lexer::Range init_where)
: init_expr(std::move(ex)), tuple_num(u), self(self), where(where), init_where(init_where)
{
    ListenToNode(init_expr.get());
    OnNodeChanged(init_expr.get(), Change::Contents);
}
void Function::LocalVariable::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (init_expr->GetType()) {
        // If we're a value we handle it at codegen time.
        auto newty = InferTypeFromExpression(init_expr->GetImplementation(), true);
        if (tuple_num) {
            if (auto tupty = dynamic_cast<TupleType*>(newty)) {
                auto tuple_access = tupty->PrimitiveAccessMember(init_expr, *tuple_num);
                newty = tuple_access->GetType()->Decay();
                variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(newty, Context{ self, where });
                construction = newty->BuildInplaceConstruction(variable, { std::move(tuple_access) }, { self, init_where });
                destructor = newty->BuildDestructorCall(variable, Context{ self, where }, true);
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
                construction = newty->BuildInplaceConstruction(variable, { init_expr }, { self, init_where });
                destructor = newty->BuildDestructorCall(variable, Context{ self, where }, true);
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
        if (init_expr->GetType()->IsComplexType() || var_type->IsReference()) return init_expr->GetValue(con);
        // If they return a simple by value, then we can just alloca it fine, no destruction needed.
        auto alloc = con.alloca_builder->CreateAlloca(var_type->GetLLVMType(con));
        alloc->setAlignment(var_type->alignment());
        con->CreateStore(init_expr->GetValue(con), alloc);
        return alloc;
    }
   
    construction->GetValue(con);
    if (!var_type->IsTriviallyDestructible())
        con.AddDestructor(destructor);
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
            if (!nested.IsTerminated(nested->GetInsertBlock()))
                stmt->GenerateCode(nested);
    });
}

Function::ReturnStatement::ReturnStatement(Function* f, std::shared_ptr<Expression> expr, Scope* current, Lexer::Range where)
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
            build = self->ReturnType->BuildInplaceConstruction(Wide::Memory::MakeUnique<ReturnEmplaceValue>(self), { ret_expr }, { self, where });
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
        con.DestroyAll(false);
        con->CreateRetVoid();
        return;
    }

    // If we return a simple type
    if (!self->ReturnType->IsComplexType()) {
        // and we already have an expression of that type
        if (ret_expr->GetType() == self->ReturnType) {
            // then great, just return it directly.
            auto val = ret_expr->GetValue(con);
            con.DestroyAll(false);
            con->CreateRet(val);
            return;
        }
        // If we have a reference to it, just load it right away.
        if (ret_expr->GetType()->IsReference(self->ReturnType)) {
            auto val = con->CreateLoad(ret_expr->GetValue(con));
            con.DestroyAll(false);
            con->CreateRet(val);
            return;
        }
        // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
        // We would fix this up, but, cannot query the complexity of a type prior to code generation.
        build = self->ReturnType->BuildValueConstruction({ ret_expr }, { self, where });
        auto val = build->GetValue(con);
        con.DestroyAll(false);
        con->CreateRet(val);
        return;
    }

    // If we return a complex type, the 0th parameter will be memory into which to place the return value.
    // build should be a function taking the memory and our ret's value and emplacing it.
    // Then return void.
    build->GetValue(con);
    con.DestroyAll(false);
    con->CreateRetVoid();
    return;
}

Function::WhileStatement::WhileStatement(std::shared_ptr<Expression> ex, Lexer::Range where, Function* s)
: cond(std::move(ex)), where(where), self(s)
{
    ListenToNode(cond.get());
    OnNodeChanged(cond.get(), Change::Contents);
}
void Function::WhileStatement::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (cond->GetType())
        boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(cond, { self, where });
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
    // If, for example, we unconditionally return or break/continue, it can happen that we were already terminated.
    if (!con.IsTerminated(con->GetInsertBlock())) {
        con.DestroyDifference(condition_context, false);
        con->CreateBr(check_bb);
    }
    con->SetInsertPoint(continue_bb);
    con.DestroyDifference(condition_context, false);
    condition_con = nullptr;
    source_con = nullptr;
}

Function::IfStatement::IfStatement(std::shared_ptr<Expression> cond, Statement* true_b, Statement* false_b, Lexer::Range where, Function* s)
: cond(std::move(cond)), true_br(std::move(true_b)), false_br(std::move(false_b)), where(where), self(s)
{
    ListenToNode(this->cond.get());
    OnNodeChanged(this->cond.get(), Change::Contents);
}
void Function::IfStatement::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed) return;
    if (cond->GetType())
        boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(cond, { self, where });
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
        });
        if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
            condition_con->CreateBr(continue_bb);
        if (false_br) {
            condition_con->SetInsertPoint(else_bb);
            condition_con.GenerateCodeAndDestroyLocals([this, continue_bb](CodegenContext& false_con) {
                false_br->GenerateCode(false_con);
            });
            if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
                condition_con->CreateBr(continue_bb);
        }
        condition_con->SetInsertPoint(continue_bb);
    });
}

Function::VariableStatement::VariableStatement(std::vector<LocalVariable*> locs, std::shared_ptr<Expression> expr)
: locals(std::move(locs)), init_expr(std::move(expr)){}
void Function::VariableStatement::GenerateCode(CodegenContext& con) {
    init_expr->GetValue(con);
    for (auto local : locals)
        local->GetValue(con);
}

std::shared_ptr<Expression> Function::Scope::LookupLocal(std::string name) {
    if (named_variables.find(name) != named_variables.end())
        return named_variables.at(name).first;
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
    while_stmt->condition_con->DestroyDifference(con, false);
    con->CreateBr(while_stmt->continue_bb);
}
Function::ContinueStatement::ContinueStatement(Scope* s) {
    // The check_bb is going to expect that the cond's memory is clear for use.
    // So clean them up before we can re-create in that space.
    while_stmt = s->GetCurrentWhile();
}
void Function::ContinueStatement::GenerateCode(CodegenContext& con) {
    while_stmt->source_con->DestroyDifference(con, false);
    con->CreateBr(while_stmt->check_bb);
}
static const std::string except_alloc = "__cxa_allocate_exception";
static const std::string free_except = "__cxa_free_exception";
static const std::string throw_except = "__cxa_throw";

struct Function::ThrowStatement::ExceptionAllocateMemory : Expression {
    ExceptionAllocateMemory(Type* t)
    : alloc(t) {
        auto&& analyzer = t->analyzer;
        if (t->alignment() > std::max(analyzer.GetDataLayout().getABIIntegerTypeAlignment(64), analyzer.GetDataLayout().getPointerABIAlignment()))
            throw std::runtime_error("EH runtime does not provide memory of enough alignment to support this exception type.");
    }
    Type* alloc;
    llvm::Value* except_memory;
    std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator destructor;
    Type* GetType() override final { return alloc->analyzer.GetLvalueType(alloc); }
    llvm::Value* ComputeValue(CodegenContext& con) override final {
        auto size_t_ty = llvm::IntegerType::get(con, alloc->analyzer.GetDataLayout().getPointerSizeInBits());
        auto allocate_exception = con.module->getFunction(except_alloc);
        if (!allocate_exception) {
            llvm::Type* args[] = { size_t_ty };
            auto fty = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(con), args, false);
            allocate_exception = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, except_alloc, con.module);
        }
        except_memory = con->CreateCall(allocate_exception, { llvm::ConstantInt::get(size_t_ty, alloc->size(), false) });
        destructor = con.AddDestructor([this](CodegenContext& con) {
            auto free_exception = con.module->getFunction(free_except);
            if (!free_exception) {
                llvm::Type* args[] = { con.GetInt8PtrTy() };
                auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(con), args, false);
                free_exception = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, free_except, con.module);
            }
            con->CreateCall(free_exception, { except_memory });
        });
        return con->CreatePointerCast(except_memory, GetType()->GetLLVMType(con));
    }
};
Function::ThrowStatement::ThrowStatement(std::shared_ptr<Expression> expr, Context c) {
    // http://mentorembedded.github.io/cxx-abi/abi-eh.html
    // 2.4.2
    ty = expr->GetType()->Decay();
    except_memory = Wide::Memory::MakeUnique<ExceptionAllocateMemory>(ty);
    // There is no longer a guarantee thas, as an argument, except_memory will be in the same CodegenContext
    // and the iterator could be invalidated. Strictly get the value in the original CodegenContext that ThrowStatement::GenerateCode
    // is called with so that we can erase the destructor later.
    exception = BuildChain(BuildChain(except_memory, ty->BuildInplaceConstruction(except_memory, { std::move(expr) }, c)), except_memory);
}
void Function::ThrowStatement::GenerateCode(CodegenContext& con) {
    auto value = exception->GetValue(con);
    // Throw this shit.
    auto cxa_throw = con.module->getFunction(throw_except);
    if (!cxa_throw) {
        llvm::Type* args[] = { con.GetInt8PtrTy(), con.GetInt8PtrTy(), con.GetInt8PtrTy() };
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(con), args, false);
        cxa_throw = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, throw_except, con);
    }
    // If we got here then creating the exception value didn't throw. Don't destroy it now.
    con.EraseDestructor(except_memory->destructor);
    llvm::Value* args[] = { con->CreatePointerCast(value, con.GetInt8PtrTy()), ty->GetRTTI(con), con->CreatePointerCast(ty->GetDestructorFunction(con), con.GetInt8PtrTy()) };
    // Do we have an existing handler to go to? If we do, then first land, then branch directly to it.
    // Else, kill everything and GTFO this function and let the EH routines worry about it.
    if (con.HasDestructors() || con.EHHandler)
        con->CreateInvoke(cxa_throw, con.GetUnreachableBlock(), con.CreateLandingpadForEH(), args);
    else
        con->CreateCall(cxa_throw, args);
}
std::shared_ptr<Statement> Function::AnalyzeStatement(const Parse::Statement* s) {
    if (auto ret = dynamic_cast<const Parse::Return*>(s)) {
        if (ret->RetExpr) {
            return Wide::Memory::MakeUnique<ReturnStatement>(this, analyzer.AnalyzeExpression(this, ret->RetExpr), current_scope, ret->location);
        }
        return Wide::Memory::MakeUnique<ReturnStatement>(this, nullptr, current_scope, ret->location);
    }

    if (auto comp = dynamic_cast<const Parse::CompoundStatement*>(s)) {
        LocalScope compound(this);
        for (auto stmt : comp->stmts)
            compound->active.push_back(AnalyzeStatement(stmt));
        return Wide::Memory::MakeUnique<CompoundStatement>(compound.s);
    }

    if (auto expr = dynamic_cast<const Parse::Expression*>(s))
        return analyzer.AnalyzeExpression(this, expr);

    if (auto var = dynamic_cast<const Parse::Variable*>(s)) {
        std::vector<LocalVariable*> locals;
        auto init_expr = analyzer.AnalyzeExpression(this, var->initializer);
        if (var->name.size() == 1) {
            auto&& name = var->name.front();
            if (current_scope->named_variables.find(var->name.front().name) != current_scope->named_variables.end())
                throw VariableShadowing(name.name, current_scope->named_variables.at(name.name).second, name.where);
            auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr, this, var->name.front().where, var->initializer->location);
            locals.push_back(var_stmt.get());
            current_scope->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
        } else {
            unsigned i = 0;
            for (auto&& name : var->name) {
                if (current_scope->named_variables.find(name.name) != current_scope->named_variables.end())
                    throw VariableShadowing(name.name, current_scope->named_variables.at(name.name).second, name.where);
                auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr, i++, this, name.where, var->initializer->location);
                locals.push_back(var_stmt.get());
                current_scope->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
            }
        }
        return Wide::Memory::MakeUnique<VariableStatement>(std::move(locals), std::move(init_expr));
    }

    if (auto whil = dynamic_cast<const Parse::While*>(s)) {
        LocalScope condscope(this);
        auto get_expr = [&, this]() -> std::shared_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(whil->var_condition));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(this, whil->condition);
        };
        auto cond = get_expr();
        auto while_stmt = Wide::Memory::MakeUnique<WhileStatement>(cond, whil->location, this);
        condscope->active.push_back(std::move(cond));
        condscope->current_while = while_stmt.get();
        LocalScope bodyscope(this);
        bodyscope->active.push_back(AnalyzeStatement(whil->body));
        while_stmt->body = bodyscope->active.back();
        return std::move(while_stmt);
    }

    if (auto break_stmt = dynamic_cast<const Parse::Break*>(s)) {
        if (!current_scope->GetCurrentWhile())
            throw NoControlFlowStatement(break_stmt->location);
        return Wide::Memory::MakeUnique<BreakStatement>(current_scope);
    }
    if (auto continue_stmt = dynamic_cast<const Parse::Continue*>(s)) {
        if (!current_scope->GetCurrentWhile())
            throw NoControlFlowStatement(continue_stmt->location);
        return Wide::Memory::MakeUnique<ContinueStatement>(current_scope);
    }

    if (auto if_stmt = dynamic_cast<const Parse::If*>(s)) {
        LocalScope condscope(this);
        auto get_expr = [&, this]() -> std::shared_ptr<Expression> {
            if (if_stmt->var_condition) {
                if (if_stmt->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(if_stmt->var_condition));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(this, if_stmt->condition);
        };
        auto cond = get_expr();
        condscope->active.push_back(cond);
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
        return Wide::Memory::MakeUnique<IfStatement>(cond, true_br, false_br, if_stmt->location, this);
    }

    if (auto thro = dynamic_cast<const Wide::Parse::Throw*>(s)) {
        if (!thro->expr)
            return Wide::Memory::MakeUnique<RethrowStatement>();
        auto expression = analyzer.AnalyzeExpression(this, thro->expr);
        return Wide::Memory::MakeUnique<ThrowStatement>(std::move(expression), Context(this, thro->location));
    }

    // Fuck yeah! Or me, at least.
    if (auto try_ = dynamic_cast<const Wide::Parse::TryCatch*>(s)) {
        std::vector<std::shared_ptr<Statement>> try_stmts;
        {
            LocalScope tryscope(this);
            for (auto stmt : try_->statements->stmts)
                try_stmts.push_back(AnalyzeStatement(stmt));
        }
        std::vector<TryStatement::Catch> catches;
        for (auto catch_ : try_->catches) {
            LocalScope catchscope(this);
            if (catch_.all) {
                std::vector<std::shared_ptr<Statement>> stmts;
                for (auto stmt : catch_.statements)
                    stmts.push_back(AnalyzeStatement(stmt));
                catches.push_back(TryStatement::Catch(nullptr, std::move(stmts), nullptr));                
                break;
            }
            auto type = analyzer.AnalyzeExpression(this, catch_.type);
            auto con = dynamic_cast<ConstructorType*>(type->GetType());
            if (!con) throw std::runtime_error("Catch parameter type was not a type.");
            auto catch_type = con->GetConstructedType();
            assert(IsLvalueType(catch_type) || dynamic_cast<PointerType*>(catch_type));
            auto target_type = IsLvalueType(catch_type)
                ? catch_type->Decay()
                : dynamic_cast<PointerType*>(catch_type)->GetPointee();
            auto catch_parameter = std::make_shared<TryStatement::CatchParameter>();
            catch_parameter->t = catch_type;
            catchscope->named_variables.insert(std::make_pair(catch_.name, std::make_pair(catch_parameter, catch_.type->location)));
            std::vector<std::shared_ptr<Statement>> stmts;
            for (auto stmt : catch_.statements)
                stmts.push_back(AnalyzeStatement(stmt));
            catches.push_back(TryStatement::Catch(catch_type, std::move(stmts), std::move(catch_parameter)));
        }
        return Wide::Memory::MakeUnique<TryStatement>(std::move(try_stmts), std::move(catches), analyzer);
    }
    assert(false && "Unsupported statement.");
}

Function::Parameter::Parameter(Function* s, unsigned n, Lexer::Range where)
: self(s), num(n), where(where)
{
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
        if (!self->Args[num]->IsTriviallyDestructible())
            self->param_destructors[num] = self->Args[num]->BuildDestructorCall(shared_from_this(), { self, where }, true);
        else
            self->param_destructors[num] = std::function<void(CodegenContext&)>();
        OnChange();
    }
}
Type* Function::Parameter::GetType() {
    return cur_ty;
}
llvm::Value* Function::Parameter::ComputeValue(CodegenContext& con) {
    auto argnum = num;
    if (self->ReturnType->IsComplexType())
        ++argnum;
    auto llvm_argument = std::next(self->llvmfunc->arg_begin(), argnum);
    
    if (self->Args[num]->IsComplexType() || self->Args[num]->IsReference())
        return llvm_argument;

    auto alloc = con.alloca_builder->CreateAlloca(self->Args[num]->GetLLVMType(con));
    alloc->setAlignment(self->Args[num]->alignment());
    con->CreateStore(llvm_argument, alloc);
    return alloc;
}

Function::Function(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* mem, std::string src_name)
: MetaType(a)
, ReturnType(nullptr)
, fun(astfun)
, context(mem)
, s(State::NotYetAnalyzed)
, root_scope(nullptr)
, source_name(src_name) {
    // Only match the non-concrete arguments.
    param_destructors.resize(args.size());
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
        auto param = std::make_shared<Parameter>(this, num++, Lexer::Range(nullptr));
        root_scope->named_variables.insert(std::make_pair("this", std::make_pair(param, Lexer::Range(nullptr))));
        root_scope->active.push_back(param);
        param->OnNodeChanged(nullptr, Change::Contents);
    }
    Args.insert(Args.end(), args.begin(), args.end());
    for(auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
        auto param = std::make_shared<Parameter>(this, num++, arg.location);
        root_scope->named_variables.insert(std::make_pair(arg.name, std::make_pair(param, arg.location)));
        root_scope->active.push_back(param);
        param->OnNodeChanged(nullptr, Change::Contents);
    }

    std::stringstream strstr;
    strstr << "__" << std::hex << this;
    name = strstr.str();

    // Deal with the attributes first, if any
    if (auto fun = dynamic_cast<const Parse::AttributeFunctionBase*>(astfun)) {
        for (auto attr : fun->attributes) {
            if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized)) {
                if (name->val == "export") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer);
                    if (auto str = dynamic_cast<StringType*>(expr->GetType()->Decay())) {
                        trampoline.push_back([str](llvm::Module* module) { return str->GetValue(); });
                        continue;
                    }
                    auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                    if (!overset)
                        throw NotAType(expr->GetType()->Decay(), attr.initializer->location);
                    auto tuanddecl = overset->GetSingleFunction();
                    if (!tuanddecl.second) throw NotAType(expr->GetType()->Decay(), attr.initializer->location);
                    auto tu = tuanddecl.first;
                    auto decl = tuanddecl.second;
                    trampoline.push_back(tu->MangleName(decl));
                    if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                        ClangContexts.insert(analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent())));
                        if (!meth->isStatic()) {
                            auto clangty = dynamic_cast<ClangType*>(analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent())));
                            auto thisty = analyzer.GetLvalueType(clangty);
                            // Add "this".
                            if (auto des = dynamic_cast<const Parse::Destructor*>(fun)) {
                                Args.push_back(analyzer.GetLvalueType(clangty));
                                param_destructors.resize(1);
                                ConstructorContext = clangty;
                            }
                            auto param = std::make_shared<Parameter>(this, 0, Lexer::Range(nullptr));
                            root_scope->named_variables.insert(std::make_pair("this", std::make_pair(param, Lexer::Range(nullptr))));
                            root_scope->active.push_back(param);
                            param->OnNodeChanged(nullptr, Change::Contents);
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
                            if (!dynamic_cast<const Parse::Destructor*>(fun))
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

    // Explicit return type, if any
    if (auto fun = dynamic_cast<const Parse::Function*>(astfun)) {
        if (fun->explicit_return) {
            auto expr = analyzer.AnalyzeExpression(this, fun->explicit_return);
            if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay()))
                ExplicitReturnType = con->GetConstructedType();
            else
                throw NotAType(expr->GetType(), fun->explicit_return->location);
        }
    }
    // Constructors and destructors, we know in advance to return void.
    if (dynamic_cast<const Parse::Constructor*>(fun) || dynamic_cast<const Parse::Destructor*>(fun))
        ExplicitReturnType = analyzer.GetVoidType();
}

Wide::Util::optional<clang::QualType> Function::GetClangType(ClangTU& where) {
    return GetSignature()->GetClangType(where);
}

void Function::ComputeBody() {
    if (s == State::NotYetAnalyzed) {

        s = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor
        if (auto con = dynamic_cast<const Parse::Constructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            std::unordered_set<const Parse::VariableInitializer*> used_initializers;
            for (auto&& x : members) {
                auto has_initializer = [&]() -> const Parse::VariableInitializer*{
                    for (auto&& init : con->initializers) {
                        // Match if it's a name and the one we were looking for.
                        auto ident = dynamic_cast<const Parse::Identifier*>(init.initialized);
                        if (x.name && ident) {
                            if (ident->val == *x.name)
                                return &init;
                        } else {
                            // Match if it's a type and the one we were looking for.
                            auto ty = analyzer.AnalyzeExpression(this, init.initialized);
                            if (auto conty = dynamic_cast<ConstructorType*>(ty->GetType())) {
                                if (conty->GetConstructedType() == x.t)
                                    return &init;
                            }
                        }
                    }
                    return nullptr;
                };
                auto num = x.num;
                auto result = analyzer.GetLvalueType(x.t);
                // Gotta get the correct this pointer.
                struct MemberConstructionAccess : Expression {
                    Type* member;
                    Lexer::Range where;
                    std::shared_ptr<Expression> Construction;
                    std::shared_ptr<Expression> memexpr;
                    std::function<void(CodegenContext&)> destructor;
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        auto val = Construction->GetValue(con);
                        if (destructor)
                            con.AddExceptionOnlyDestructor(destructor);
                        return val;
                    }
                    Type* GetType() override final {
                        return Construction->GetType();
                    }
                    MemberConstructionAccess(Type* mem, Lexer::Range where, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> memexpr)
                        : member(mem), where(where), Construction(std::move(expr)), memexpr(memexpr) 
                    {
                        if (!member->IsTriviallyDestructible())
                            destructor = member->BuildDestructorCall(memexpr, { member, where }, true);
                    }
                };
                auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, num.offset);
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
                auto make_member_initializer = [&, this](std::vector<std::shared_ptr<Expression>> init, Lexer::Range where) {
                    auto construction = x.t->BuildInplaceConstruction(member, std::move(init), { this, where });
                    return Wide::Memory::MakeUnique<MemberConstructionAccess>(x.t, where, std::move(construction), member);
                };
                if (auto init = has_initializer()) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    used_initializers.insert(init);
                    if (init->initializer) {
                        // If it's a tuple, pass each subexpression.
                        std::vector<std::shared_ptr<Expression>> exprs;
                        if (auto tup = dynamic_cast<const Parse::Tuple*>(init->initializer)) {
                            for (auto expr : tup->expressions)
                                exprs.push_back(analyzer.AnalyzeExpression(this, expr));
                        } else
                            exprs.push_back(analyzer.AnalyzeExpression(this, init->initializer));
                        root_scope->active.push_back(make_member_initializer(std::move(exprs), init->where));
                    }
                    else
                        root_scope->active.push_back(make_member_initializer({}, init->where));
                    continue;
                }
                // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                if (x.InClassInitializer) {
                    root_scope->active.push_back(make_member_initializer({ x.InClassInitializer(LookupLocal("this")) }, x.location));
                    continue;
                }
                root_scope->active.push_back(make_member_initializer({}, fun->where));
            }
            for (auto&& x : con->initializers) {
                if (used_initializers.find(&x) == used_initializers.end()) {
                    if (auto ident = dynamic_cast<const Parse::Identifier*>(x.initialized))
                        throw NoMemberToInitialize(context->Decay(), ident->val, x.where);
                    auto expr = analyzer.AnalyzeExpression(this, x.initializer);
                    auto conty = dynamic_cast<ConstructorType*>(expr->GetType());
                    throw NoMemberToInitialize(context->Decay(), conty->GetConstructedType()->explain(), x.where);
                }
            }
            // set the vptrs if necessary
            auto ty = dynamic_cast<Type*>(member);
            root_scope->active.push_back(ty->SetVirtualPointers(LookupLocal("this")));
        }

        // Now the body.
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            root_scope->active.push_back(AnalyzeStatement(fun->statements[i]));
        }        

        // If we were a destructor, destroy.
        if (auto des = dynamic_cast<const Parse::Destructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            for (auto rit = members.rbegin(); rit != members.rend(); ++rit) {
                struct DestructorCall : Expression {
                    DestructorCall(std::function<void(CodegenContext&)> destructor, Analyzer& a)
                    : destructor(destructor), a(&a) {}
                    std::function<void(CodegenContext&)> destructor;
                    Analyzer* a;
                    Type* GetType() override final {
                        return a->GetVoidType();
                    }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        destructor(con);
                        return nullptr;
                    }
                };
                auto num = rit->num;
                auto result = analyzer.GetLvalueType(rit->t);
                auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, num.offset);
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
                root_scope->active.push_back(std::make_shared<DestructorCall>(rit->t->BuildDestructorCall(member, { this, fun->where }, true), analyzer));
            }
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
    } else
        ReturnType = *ExplicitReturnType;
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
    for (auto des : param_destructors)
        if (des)
            c.AddDestructor(des);
    for (auto&& stmt : root_scope->active)
        if (!c.IsTerminated(c->GetInsertBlock()))
            stmt->GenerateCode(c);

    if (!c.IsTerminated(irbuilder.GetInsertBlock())) {
        if (ReturnType == analyzer.GetVoidType()) {
            c.DestroyAll(false);
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

std::shared_ptr<Expression> Function::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    struct Self : public Expression {
        Self(Function* self, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> val)
        : self(self), val(std::move(val))
        {
            if (!expr) return;
            if (auto func = dynamic_cast<const Parse::DynamicFunction*>(self->fun)) {
                udt = dynamic_cast<UserDefinedType*>(expr->GetType()->Decay());
                if (!udt) 
                    return;
                obj = udt->GetVirtualPointer(expr);
            }
        }
        UserDefinedType* udt;
        std::shared_ptr<Expression> obj;
        std::shared_ptr<Expression> val;
        Function* self;
        Type* GetType() override final {
            return self->GetSignature();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            if (!self->llvmfunc)
                self->EmitCode(con);
            val->GetValue(con);
            if (obj) {
                auto func = dynamic_cast<const Parse::DynamicFunction*>(self->fun);
                assert(func);
                auto vindex = udt->GetVirtualFunctionIndex(func);
                if (!vindex) return self->llvmfunc;
                auto vptr = con->CreateLoad(obj->GetValue(con));
                return con->CreatePointerCast(con->CreateLoad(con->CreateConstGEP1_32(vptr, *vindex)), self->GetSignature()->GetLLVMType(con));
            }
            return self->llvmfunc;
        }
    };
    auto self = !args.empty() ? args[0] : nullptr;
   
    return GetSignature()->BuildCall(Wide::Memory::MakeUnique<Self>(this, self, std::move(val)), std::move(args), c);
}

std::shared_ptr<Expression> Function::LookupLocal(std::string name) {
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
            args += fun->args[i++ - Args.size() != fun->args.size()].name + " := ";
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
void Function::TryStatement::GenerateCode(CodegenContext& con) {
    auto source_block = con->GetInsertBlock();

    auto try_con = con;
    auto catch_block = llvm::BasicBlock::Create(con, "catch_block", con->GetInsertBlock()->getParent());
    auto dest_block = llvm::BasicBlock::Create(con, "dest_block", con->GetInsertBlock()->getParent());

    con->SetInsertPoint(catch_block);
    auto phi = con->CreatePHI(con.GetLpadType(), 0);
    std::vector<llvm::Constant*> rttis;
    for (auto&& catch_ : catches) {
        if (catch_.t)
            rttis.push_back(catch_.t->GetRTTI(con));
    }
    try_con.EHHandler = CodegenContext::EHScope{ &con, catch_block, phi, rttis};
    try_con->SetInsertPoint(source_block);

    for (auto&& stmt : statements)
        if (!try_con.IsTerminated(try_con->GetInsertBlock()))
            stmt->GenerateCode(try_con);
    if (!try_con.IsTerminated(try_con->GetInsertBlock()))
        try_con->CreateBr(dest_block);
    if (phi->getNumIncomingValues() == 0) {
        phi->removeFromParent();
        catch_block->removeFromParent();
        con->SetInsertPoint(dest_block);
        return;
    }

    // Generate the code for all the catch statements.
    auto for_ = llvm::Intrinsic::getDeclaration(con, llvm::Intrinsic::eh_typeid_for);
    auto catch_con = con;
    catch_con->SetInsertPoint(catch_block);
    auto selector = catch_con->CreateExtractValue(phi, { 1 });
    auto except_object = catch_con->CreateExtractValue(phi, { 0 });

    auto catch_ender = [](CodegenContext& con) {
        con->CreateCall(con.GetCXAEndCatch());
    };
    for (auto&& catch_ : catches) {
        CodegenContext catch_block_con(catch_con);
        if (!catch_.t) {
            for (auto&& stmt : catch_.stmts)
                if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                    stmt->GenerateCode(catch_block_con);
            if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock())) {
                con.DestroyDifference(catch_block_con, false);
                catch_block_con->CreateBr(dest_block);
            }
            break;
        }
        auto catch_target = llvm::BasicBlock::Create(con, "catch_target", catch_block_con->GetInsertBlock()->getParent());
        auto catch_continue = llvm::BasicBlock::Create(con, "catch_continue", catch_block_con->GetInsertBlock()->getParent());
        auto target_selector = catch_block_con->CreateCall(for_, { catch_.t->GetRTTI(con) });
        auto result = catch_block_con->CreateICmpEQ(selector, target_selector);
        catch_block_con->CreateCondBr(result, catch_target, catch_continue);
        catch_block_con->SetInsertPoint(catch_target);
        // Call __cxa_begin_catch and get our result. We don't need __cxa_get_exception_ptr as Wide cannot catch by value.
        auto except = catch_block_con->CreateCall(catch_block_con.GetCXABeginCatch(), { except_object });
        catch_.catch_param->param = except;
        // Ensure __cxa_end_catch is called.
        catch_block_con.AddDestructor(catch_ender);

        for (auto&& stmt : catch_.stmts)
            if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                stmt->GenerateCode(catch_block_con);
        if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock())) {
            con.DestroyDifference(catch_block_con, false);
            catch_block_con->CreateBr(dest_block);
        }
        catch_con->SetInsertPoint(catch_continue);
    }
    // If we had no catch all, then we need to clean up and rethrow to the next try.
    if (catches.back().t) {
        auto except = catch_con->CreateCall(catch_con.GetCXABeginCatch(), { except_object });
        catch_con.AddDestructor(catch_ender);
        catch_con->CreateInvoke(catch_con.GetCXARethrow(), catch_con.GetUnreachableBlock(), catch_con.CreateLandingpadForEH());
    }
    con->SetInsertPoint(dest_block);
}
void Function::RethrowStatement::GenerateCode(CodegenContext& con) {
    if (con.HasDestructors() || con.EHHandler)
        con->CreateInvoke(con.GetCXARethrow(), con.GetUnreachableBlock(), con.CreateLandingpadForEH());
    else
        con->CreateCall(con.GetCXARethrow());
}