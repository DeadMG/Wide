#include <Wide/Semantic/Function.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
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

struct Function::LocalVariable : public Expression {
    LocalVariable(Expression* ex, Function* self, Lexer::Range where)
    : init_expr(std::move(ex)), self(self), where(where)
    {
        ListenToNode(init_expr);
        OnNodeChanged(init_expr, Change::Contents);
    }
    LocalVariable(Expression* ex, unsigned u, Function* self, Lexer::Range where)
        : init_expr(std::move(ex)), tuple_num(u), self(self), where(where)
    {
        ListenToNode(init_expr);
        OnNodeChanged(init_expr, Change::Contents);
    }

    void OnNodeChanged(Node* n, Change what) override final {
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

    std::unique_ptr<Expression> construction;
    Wide::Util::optional<unsigned> tuple_num;
    Type* tup_ty = nullptr;
    Type* var_type = nullptr;
    std::unique_ptr<ImplicitTemporaryExpr> variable;
    Expression* init_expr;
    Function* self;
    Lexer::Range where;

    void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        if (variable)
            variable->DestroyLocals(g, bb);
        if (construction)
            construction->DestroyLocals(g, bb);
    }
    llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        if (init_expr->GetType() == var_type) {
            // If they return a complex value by value, just steal it.
            if (init_expr->GetType()->IsComplexType(g) || var_type->IsReference()) return init_expr->GetValue(g, bb);
            // If they return a simple by value, then we can just alloca it fine, no destruction needed.
            auto alloc = bb.CreateAlloca(var_type->GetLLVMType(g));
            alloc->setAlignment(var_type->alignment());
            bb.CreateStore(init_expr->GetValue(g, bb), alloc);
            return alloc;
        }
       
        construction->GetValue(g, bb);
        return variable->GetValue(g, bb);
    }
    Type* GetType() override final {
        if (var_type) {
            if (var_type->IsReference())
                return self->analyzer.GetLvalueType(var_type->Decay());
            return self->analyzer.GetLvalueType(var_type);
        }
        return nullptr;
    }
};
struct Function::Scope {
    Scope(Scope* s) : parent(s), current_while(nullptr) {
        if (parent)
            parent->children.push_back(std::unique_ptr<Scope>(this));
    }
    Scope* parent;
    std::vector<std::unique_ptr<Scope>> children;
    std::unordered_map<std::string, std::pair<std::unique_ptr<Expression>, Lexer::Range>> named_variables;
    std::vector<std::unique_ptr<Statement>> active;
    WhileStatement* current_while;
    std::unique_ptr<Expression> LookupLocal(std::string name);

    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> DestroyLocals() {
        std::vector<Statement*> current;
        for (auto&& stmt : active)
            current.push_back(stmt.get());
        return [current](Codegen::Generator& g, llvm::IRBuilder<>& bb) {
            for (auto stmt : current)
                stmt->DestroyLocals(g, bb);
        };
    }
    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> DestroyAllLocals() {
        auto local = DestroyLocals();
        if (parent) {
            auto uppers = parent->DestroyAllLocals();
            return [local, uppers](Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                local(g, bb);
                uppers(g, bb);
            };
        }
        return local;
    }

    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> DestroyWhileBody() {
        // The while is attached to the CONDITION SCOPE
        // We only want to destroy the body.
        if (parent) {
            auto local = DestroyLocals();
            if (parent->current_while)
                return local;
            auto uppers = parent->DestroyWhileBody();
            return [local, uppers](Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                local(g, bb);
                uppers(g, bb);
            };            
        }
        throw std::runtime_error("fuck");
    }

    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> DestroyWhileBodyAndCond() {
        // The while is attached to the CONDITION SCOPE
        // So keep going until we have it.
        if (parent) {
            auto local = DestroyLocals();
            if (current_while)
                return local;
            auto uppers = parent->DestroyWhileBodyAndCond();
            return [local, uppers](Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                local(g, bb);
                uppers(g, bb);
            };
        }
        throw std::runtime_error("fuck");
    }

    WhileStatement* GetCurrentWhile() {
        if (current_while)
            return current_while;
        if (parent)
            return parent->GetCurrentWhile();
        return nullptr;
    }
};

Function::~Function() {}

struct Function::CompoundStatement : public Statement {
    CompoundStatement(Scope* s)
        : s(s) {}
    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    Scope* s;
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        for (auto&& stmt : s->active)
            stmt->GenerateCode(g, bb);
        for (auto rit = s->active.rbegin(); rit != s->active.rend(); ++rit)
            rit->get()->DestroyLocals(g, bb);
    }
};

struct Function::ReturnStatement : public Statement {
    ReturnStatement(Function* f, std::unique_ptr<Expression> expr, Scope* current, Lexer::Range where)
    : self(f), ret_expr(std::move(expr)), where(where)
    {
        self->returns.insert(this);
        if (ret_expr) {
            ListenToNode(ret_expr.get());
        }
        auto scope_destructors = current->DestroyAllLocals();
        destructors = [this, scope_destructors](Codegen::Generator& g, llvm::IRBuilder<>& bb) {
            if (ret_expr)
                ret_expr->DestroyLocals(g, bb);
            scope_destructors(g, bb);
        };
        OnNodeChanged(ret_expr.get());
    }
    void OnNodeChanged(Node* n) {
        if (self->ReturnType != self->analyzer.GetVoidType()) {
            struct ReturnEmplaceValue : Expression {
                ReturnEmplaceValue(Function* f)
                : self(f) {}
                Function* self;
                Type* GetType() override final {
                    return self->analyzer.GetLvalueType(self->ReturnType);
                }
                void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {}
                llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
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
    Lexer::Range where;
    Function* self;
    std::function<void(Codegen::Generator& g, llvm::IRBuilder<>&)> destructors;
    std::unique_ptr<Expression> ret_expr;
    std::unique_ptr<Expression> build;
    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        // When we're codegenned, there's no more need to destroy *any* locals, let alone ours.
    }
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        // Consider the simple cases first.
        // If our return type is void
        if (self->ReturnType == self->analyzer.GetVoidType()) {
            // If we have a void-returning expression, evaluate it, destroy it, then return.
            if (ret_expr) {
                ret_expr->GetValue(g, bb);
            }
            destructors(g, bb);
            bb.CreateRetVoid();
            return;
        }

        // If we return a simple type
        if (!self->ReturnType->IsComplexType(g)) {
            // and we already have an expression of that type
            if (ret_expr->GetType() == self->ReturnType) {
                // then great, just return it directly.
                auto val = ret_expr->GetValue(g, bb);
                destructors(g, bb);
                bb.CreateRet(val);
                return;
            }
            // If we have a reference to it, just load it right away.
            if (ret_expr->GetType()->IsReference(self->ReturnType)) {
                auto val = bb.CreateLoad(ret_expr->GetValue(g, bb));
                destructors(g, bb);
                bb.CreateRet(val);
                return;
            }
            // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
            // We would fix this up, but, cannot query the complexity of a type prior to code generation.
            build = self->ReturnType->BuildValueConstruction(Expressions(Wide::Memory::MakeUnique<ExpressionReference>(ret_expr.get())), { self, where });
            auto val = build->GetValue(g, bb);
            build->DestroyLocals(g, bb);
            destructors(g, bb);
            bb.CreateRet(val);
            return;
        }

        // If we return a complex type, the 0th parameter will be memory into which to place the return value.
        // build should be a function taking the memory and our ret's value and emplacing it.
        // Then return void.
        build->GetValue(g, bb);
        build->DestroyLocals(g, bb);
        destructors(g, bb);
        bb.CreateRetVoid();
        return;
    }
};

bool is_terminated(llvm::BasicBlock* bb) {
    return !bb->empty() && bb->back().isTerminator();
}

struct Function::WhileStatement : public Statement {
    WhileStatement(Expression* ex, Lexer::Range where, Function* s)
    : cond(std::move(ex)), where(where), self(s)
    {
        ListenToNode(cond);
        OnNodeChanged(cond, Change::Contents);
    }
    void OnNodeChanged(Node* n, Change what) override final {
        if (what == Change::Destroyed) return;
        if (cond->GetType())
            boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(Wide::Memory::MakeUnique<ExpressionReference>(cond), { self, where });
    }

    Function* self;
    Lexer::Range where;
    Expression* cond;
    Statement* body;
    std::unique_ptr<Expression> boolconvert;
    llvm::BasicBlock* continue_bb = nullptr;
    llvm::BasicBlock* check_bb = nullptr;

    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        check_bb = llvm::BasicBlock::Create(bb.getContext(), "check_bb", bb.GetInsertBlock()->getParent());
        auto loop_bb = llvm::BasicBlock::Create(bb.getContext(), "loop_bb", bb.GetInsertBlock()->getParent());
        continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
        bb.CreateBr(check_bb);
        bb.SetInsertPoint(check_bb);
        // Both of these branches need to destroy the cond's locals.
        // In the case of the loop, so that when check_bb is re-entered, it's clear.
        // In the case of the continue, so that the check's locals are cleaned up properly.
        auto condition = boolconvert->GetValue(g, bb);
        if (condition->getType() == llvm::Type::getInt8Ty(g.module->getContext()))
            condition = bb.CreateTrunc(condition, llvm::Type::getInt1Ty(g.module->getContext()));
        bb.CreateCondBr(condition, loop_bb, continue_bb);
        bb.SetInsertPoint(loop_bb);
        body->GenerateCode(g, bb);
        body->DestroyLocals(g, bb);
        cond->DestroyLocals(g, bb);
        // If, for example, we unconditionally return or break/continue, it can happen that we were already terminated.
        if (!is_terminated(bb.GetInsertBlock()))
            bb.CreateBr(check_bb);
        bb.SetInsertPoint(continue_bb);
        cond->DestroyLocals(g, bb);
    }
};
struct Function::IfStatement : public Statement{
    IfStatement(Expression* cond, Statement* true_b, Statement* false_b, Lexer::Range where, Function* s)
    : cond(std::move(cond)), true_br(std::move(true_b)), false_br(std::move(false_b)), where(where), self(s)
    {
        ListenToNode(cond);
        OnNodeChanged(cond, Change::Contents);
    }
    void OnNodeChanged(Node* n, Change what) override final {
        if (what == Change::Destroyed) return;
        if (cond->GetType())
            boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(Wide::Memory::MakeUnique<ExpressionReference>(cond), { self, where });
    }

    Function* self; 
    Lexer::Range where;
    Expression* cond;
    Statement* true_br;
    Statement* false_br;
    std::unique_ptr<Expression> boolconvert;

    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        auto true_bb = llvm::BasicBlock::Create(bb.getContext(), "true_bb", bb.GetInsertBlock()->getParent());
        auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
        auto else_bb = false_br ? llvm::BasicBlock::Create(bb.getContext(), "false_bb", bb.GetInsertBlock()->getParent()) : continue_bb;
        auto condition = boolconvert->GetValue(g, bb);
        if (condition->getType() == llvm::Type::getInt8Ty(g.module->getContext()))
            condition = bb.CreateTrunc(condition, llvm::Type::getInt1Ty(g.module->getContext()));
        bb.CreateCondBr(condition, true_bb, else_bb);
        bb.SetInsertPoint(true_bb);
        true_br->GenerateCode(g, bb);
        true_br->DestroyLocals(g, bb);
        if (!is_terminated(bb.GetInsertBlock()))
            bb.CreateBr(continue_bb);

        if (false_br) {
            bb.SetInsertPoint(else_bb);
            false_br->GenerateCode(g, bb);
            false_br->DestroyLocals(g, bb);
            if (!is_terminated(bb.GetInsertBlock()))
                bb.CreateBr(continue_bb);
        }

        bb.SetInsertPoint(continue_bb);
        cond->DestroyLocals(g, bb);
    }
};
struct Function::VariableStatement : public Statement {
    VariableStatement(std::vector<LocalVariable*> locs, std::unique_ptr<Expression> expr)
    : locals(std::move(locs)), init_expr(std::move(expr)){}
    std::unique_ptr<Expression> init_expr;
    std::vector<LocalVariable*> locals;
    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
        for (auto local : locals)
            local->DestroyLocals(g, bb);
        init_expr->DestroyLocals(g, bb);
    }
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
        init_expr->GetValue(g, bb);
        for (auto local : locals)
            local->GetValue(g, bb);
    }
};
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
struct Function::BreakStatement : public Statement {
    BreakStatement(Scope* s) {
        // The continue_bb already has code to destroy the condition's locals.
        // So don't destroy them again here.
        destroy_locals = s->DestroyWhileBody();
        while_stmt = s->GetCurrentWhile();
    }
    WhileStatement* while_stmt;
    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> destroy_locals;
    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {}
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        destroy_locals(g, bb);
        bb.CreateBr(while_stmt->continue_bb);
    }
};
struct Function::ContinueStatement : public Statement {
    ContinueStatement(Scope* s) {
        // The check_bb is going to expect that the cond's memory is clear for use.
        // So clean them up before we can re-create in that space.
        destroy_locals = s->DestroyWhileBodyAndCond();
        while_stmt = s->GetCurrentWhile();
    }
    WhileStatement* while_stmt;
    std::function<void(Codegen::Generator&, llvm::IRBuilder<>&)> destroy_locals;
    void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {}
    void GenerateCode(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        destroy_locals(g, bb);
        bb.CreateBr(while_stmt->check_bb);
    }
};
std::unique_ptr<Statement> Function::AnalyzeStatement(const AST::Statement* s) {
    if (auto ret = dynamic_cast<const AST::Return*>(s)) {
        if (ret->RetExpr) {
            return Wide::Memory::MakeUnique<ReturnStatement>(this, AnalyzeExpression(this, ret->RetExpr, analyzer), current_scope, ret->location);
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
        return AnalyzeExpression(this, expr, analyzer);

    if (auto var = dynamic_cast<const AST::Variable*>(s)) {
        std::vector<LocalVariable*> locals;
        auto init_expr = AnalyzeExpression(this, var->initializer, analyzer);
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
            return AnalyzeExpression(this, whil->condition, analyzer);
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
            return AnalyzeExpression(this, if_stmt->condition, analyzer);
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

    assert(false && "Unsupported statement.");
}

struct Function::Parameter : public Expression {
    Parameter(Function* s, unsigned n, Lexer::Range where)
    : self(s), num(n), where(where)
    {
        OnNodeChanged(s);
        ListenToNode(self);
    }
    Lexer::Range where;
    Function* self;
    unsigned num;
    std::unique_ptr<Expression> destructor;
    Type* cur_ty = nullptr;

    void OnNodeChanged(Node* n) {
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
    Type* GetType() override final {
        return cur_ty;
    }
    void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        if (self->Args[num]->IsComplexType(g))
            destructor->GetValue(g, bb);
    }

    llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
        auto argnum = num;
        if (self->ReturnType->IsComplexType(g))
            ++argnum;
        auto llvm_argument = std::next(self->llvmfunc->arg_begin(), argnum);

        if (self->Args[num]->IsComplexType(g) || self->Args[num]->IsReference())
            return llvm_argument;

        auto alloc = bb.CreateAlloca(self->Args[num]->GetLLVMType(g));
        alloc->setAlignment(self->Args[num]->alignment());
        bb.CreateStore(llvm_argument, alloc);
        return alloc;
    }
};

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
    if (mem && (dynamic_cast<MemberFunctionContext*>(mem->Decay()))) {
        Args.push_back(args[0] == args[0]->Decay() ? a.GetLvalueType(args[0]) : args[0]);
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
            auto expr = AnalyzeExpression(this, ass->rhs, analyzer);
            if (ident->val == "ExportName") {
                auto str = dynamic_cast<StringType*>(expr->GetType()->Decay());
                if (!str)
                    throw PrologExportNotAString(ass->rhs->location);
                trampoline.push_back([str](Codegen::Generator& g) { return str->GetValue(); });
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
                if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl))
                    ClangContexts.insert(analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent())));
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
            auto member = dynamic_cast<UserDefinedType*>(context->Decay());
            auto members = member->GetMembers();
            for (auto&& x : members) {
                if (x.vptr) {
                    root_scope->active.push_back(x.InClassInitializer(LookupLocal("this")));
                    continue;
                }
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
                // For member variables, don't add them to the list, the destructor will handle them.
                auto member = CreatePrimUnOp(LookupLocal("this"), analyzer.GetLvalueType(x.t), [num](llvm::Value* val, Codegen::Generator& g, llvm::IRBuilder<>& bb) -> llvm::Value* {
                    return bb.CreateStructGEP(val, num);
                });
                if (auto init = has_initializer(x.name)) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    
                    if (init->initializer)
                        root_scope->active.push_back(x.t->BuildInplaceConstruction(std::move(member), Expressions(AnalyzeExpression(this, init->initializer, analyzer)), { this, init->location }));
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
            for (auto&& x : con->initializers)
                if (std::find_if(members.begin(), members.end(), [&](decltype(*members.begin())& ref) { return ref.name == x->name.front().name; }) == members.end())
                    throw NoMemberToInitialize(member, x->name.front().name, x->location);
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
        }
    }
}

void Function::EmitCode(Codegen::Generator& g) {
    if (llvmfunc)
        return;
    auto sig = GetSignature();
    auto llvmsig = sig->GetLLVMType(g);
    llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, name, g.module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(g.module->getContext(), "entry", llvmfunc);
    llvm::IRBuilder<> irbuilder(bb);
    for (auto&& stmt : root_scope->active)
        stmt->GenerateCode(g, irbuilder);

    if (!is_terminated(irbuilder.GetInsertBlock())) {
        if (ReturnType == analyzer.GetVoidType()) {
            current_scope->DestroyAllLocals()(g, irbuilder);
            irbuilder.CreateRetVoid();
        }
        else
            irbuilder.CreateUnreachable();
    }

    if (llvm::verifyFunction(*llvmfunc, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");

    for (auto exportnam : trampoline) {
        auto exportname = exportnam(g);
        // oh yay.
        // Gotta handle ABI mismatch.

        // Check Clang's uses of this function - if it bitcasts them all we're good.

        auto ourret = llvmfunc->getFunctionType()->getReturnType();
        auto int8ty = llvm::IntegerType::getInt8Ty(g.module->getContext());
        auto int1ty = llvm::IntegerType::getInt1Ty(g.module->getContext());
        llvm::Type* trampret;
        llvm::Function* tramp = nullptr;
        std::vector<llvm::Value*> args;
        if (tramp = g.module->getFunction(exportname)) {
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
                    } else
                        CastType = cast->getDestTy();
                }
                if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use)) {
                    if (CastType) {
                        if (CastType != constant->getType()) {
                            throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
                        }
                    } else
                        CastType = constant->getType();
                }
            }
            // There are no uses that are invalid.
            if (CastType || std::distance(tramp->use_begin(), tramp->use_end()) == 0) {
                if (!CastType) CastType = llvmfunc->getType();
                tramp->setName("__fucking__clang__type_hacks");
                auto badf = tramp;
                auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(CastType)->getElementType());
                tramp = llvm::Function::Create(t, llvm::GlobalValue::LinkageTypes::ExternalLinkage, exportname, g.module.get());
                // Update all Clang's uses
                for (auto use_it = badf->use_begin(); use_it != badf->use_end(); ++use_it) {
                    auto use = *use_it;
                    if (auto cast = llvm::dyn_cast<llvm::CastInst>(use))
                        cast->replaceAllUsesWith(tramp);
                    if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use))
                        constant->replaceAllUsesWith(tramp);
                }
            }

            trampret = tramp->getFunctionType()->getReturnType();
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(g.module->getContext(), "entry", tramp);
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
                    if (Args[i]->IsEliminateType()) {
                        args.push_back(llvm::Constant::getNullValue(ty->getParamType(i)));
                        continue;
                    }

                    throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
                }
                if (ourret != trampret) {
                    if (ourret != llvm::IntegerType::getInt8Ty(g.module->getContext()) || trampret != llvm::IntegerType::getInt1Ty(g.module->getContext()))
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
                fty = llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(g.module->getContext()), false);
            tramp = llvm::Function::Create(fty, llvm::GlobalValue::ExternalLinkage, exportname, g.module.get());
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(g.module->getContext(), "entry", tramp);
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
                auto udt = dynamic_cast<UserDefinedType*>(expr->GetType()->Decay());
                if (!udt) return;
                auto vindex = udt->GetVirtualFunctionIndex(func);
                if (!vindex) return;
                index = *vindex;
                obj = udt->GetVirtualPointer(Wide::Memory::MakeUnique<ExpressionReference>(expr));
            }
        }
        unsigned index;
        std::unique_ptr<Expression> obj;
        std::unique_ptr<Expression> val;
        Function* self;
        Type* GetType() override final {
            return self->GetSignature();
        }
        llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            if (!self->llvmfunc)
                self->EmitCode(g);
            val->GetValue(g, bb);
            if (obj) {
                auto vptr = bb.CreateLoad(obj->GetValue(g, bb));
                return bb.CreatePointerCast(bb.CreateLoad(bb.CreateConstGEP1_32(vptr, index)), self->GetSignature()->GetLLVMType(g));
            }
            return self->llvmfunc;
        }
        void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {}
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
    return analyzer.GetFunctionType(ReturnType, Args, false);
}

Type* Function::GetConstantContext() {
    return nullptr;
}
std::string Function::explain() {
    auto args = std::string("(");
    for (auto& ty : Args) {
        if (&ty != &Args.back())
            args += ty->explain() + ", ";
        else
            args += ty->explain();
    }
    args += ")";

    std::string context_name = context->explain() + "." + source_name;
    if (context == analyzer.GetGlobalModule())
        context_name = source_name;
    return "(" + context_name + args + " at " + fun->where + ")";
}
