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

struct Function::LocalVariable : public Expression {
    LocalVariable(Expression* ex, Analyzer& a)
    : init_expr(std::move(ex)), analyzer(a)
    {
        OnNodeChanged(init_expr);
    }
    LocalVariable(Expression* ex, unsigned u, Analyzer& a)
        : init_expr(std::move(ex)), tuple_num(u), analyzer(a)
    {
        OnNodeChanged(init_expr);
    }

    void OnNodeChanged(Node* n) override final {
        if (init_expr->GetType()) {
            auto newty = init_expr->GetType()->Decay();
            if (tuple_num) {
                if (auto tupty = dynamic_cast<TupleType*>(newty)) {
                    tuple_access = tupty->PrimitiveAccessMember(init_expr, *tuple_num);
                    newty = tuple_access->GetType()->Decay();
                    construction = newty->BuildInplaceConstruction([this] { return alloc; }, { tuple_access.get() });
                    destruct = newty->BuildDestructorCall();
                    if (newty != var_type) {
                        OnChange();
                    }
                    return;
                }
                throw std::runtime_error("fuck");
            }
            if (newty != var_type) {
                if (newty) {
                    construction = newty->BuildInplaceConstruction([this] { return alloc; }, { init_expr });
                    destruct = newty->BuildDestructorCall();
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

    std::unique_ptr<Expression> tuple_access;
    std::unique_ptr<Expression> construction;
    Wide::Util::optional<unsigned> tuple_num;
    Type* tup_ty = nullptr;
    Type* var_type = nullptr;
    llvm::Value* alloc = nullptr;
    Expression* init_expr;
    std::function<void(llvm::Value*, Codegen::Generator& g)> destruct;
    Analyzer& analyzer;

    void DestroyLocals(Codegen::Generator& g) {
        destruct(alloc, g);
        // If any temporaries were created during construction, destroy them.
        construction->DestroyLocals(g);
    }
    llvm::Value* ComputeValue(Codegen::Generator& g) {
        alloc = g.builder.CreateAlloca(var_type->GetLLVMType(g));
        return construction->GetValue(g);
    }
    Type* GetType() {
        if (var_type)
            return analyzer.GetLvalueType(var_type);
        return nullptr;
    }
};
struct Function::Scope {
    Scope(Scope* s) : parent(s) {
        if (parent)
            parent->children.push_back(std::unique_ptr<Scope>(this));
    }
    Scope* parent;
    std::vector<std::unique_ptr<Scope>> children;
    std::unordered_map<std::string, std::pair<std::unique_ptr<Expression>, Lexer::Range>> named_variables;
    std::vector<Statement*> active;
    WhileStatement* current_while;

    std::function<void(Codegen::Generator&)> DestroyLocals() {
        auto copy = active;
        return [copy](Codegen::Generator& g) {
            for (auto stmt : copy)
                stmt->DestroyLocals(g);
        };
    }
    std::function<void(Codegen::Generator&)> DestroyAllLocals() {
        auto local = DestroyLocals();
        if (parent) {
            auto uppers = parent->DestroyAllLocals();
            return [local, uppers](Codegen::Generator& g) {
                local(g);
                uppers(g);
            };
        }
        return local;
    }

    std::function<void(Codegen::Generator&)> DestroyWhileBody() {
        // The while is attached to the CONDITION SCOPE
        // We only want to destroy the body.
        if (parent) {
            auto local = DestroyLocals();
            if (parent->current_while)
                return local;
            auto uppers = parent->DestroyWhileBody();
            return [local, uppers](Codegen::Generator& g) {
                local(g);
                uppers(g);
            };            
        }
        throw std::runtime_error("fuck");
    }

    std::function<void(Codegen::Generator&)> DestroyWhileBodyAndCond() {
        // The while is attached to the CONDITION SCOPE
        // So keep going until we have it.
        if (parent) {
            auto local = DestroyLocals();
            if (current_while)
                return local;
            auto uppers = parent->DestroyWhileBodyAndCond();
            return [local, uppers](Codegen::Generator& g) {
                local(g);
                uppers(g);
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
    CompoundStatement(std::vector<std::unique_ptr<Statement>> stmts)
        : statements(std::move(stmts)) {}
    std::vector<std::unique_ptr<Statement>> statements;
    void DestroyLocals(Codegen::Generator& g) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    void GenerateCode(Codegen::Generator& g) override final {
        for (auto&& stmt : statements)
            stmt->GenerateCode(g);
        // Destroy all the locals after executing the statements.
        // Unless the block was already terminated in which case they're already destroyed.
        if (!g.builder.GetInsertBlock()->getTerminator())
            for (auto rit = statements.rbegin(); rit != statements.rend(); ++rit)
                rit->get()->DestroyLocals(g);
    }
};

struct Function::ReturnStatement : public Statement {
    ReturnStatement(Function* f, std::unique_ptr<Expression> expr, Scope* current)
    : self(f), ret_expr(std::move(expr))
    {
        self->returns.insert(this);
        if (ret_expr) {
            current->active.push_back(ret_expr.get());
            ListenToNode(ret_expr.get());
        }
        destructors = current->DestroyAllLocals();
        OnNodeChanged(ret_expr.get());
    }
    void OnNodeChanged(Node* n) {
        if (self->ReturnType != self->analyzer.GetVoidType()) {
            if (!self->ReturnType->IsComplexType()) {
                if (ret_expr && ret_expr->GetType() && ret_expr->GetType() != self->ReturnType) {
                    build = self->ReturnType->BuildValueConstruction({ ret_expr.get() });
                }
                return;
            }
            build = self->ReturnType->BuildInplaceConstruction([this] { return self->llvmfunc->arg_begin(); }, { ret_expr.get() });
        } else
            build = nullptr;
    }
    Function* self;
    std::function<void(Codegen::Generator& g)> destructors;
    std::unique_ptr<Expression> ret_expr;
    std::unique_ptr<Expression> build;
    void DestroyLocals(Codegen::Generator& g) override final {
        // When we're codegenned, there's no more need to destroy *any* locals, let alone ours.
    }
    void GenerateCode(Codegen::Generator& g) override final {
        // Consider the simple cases first.
        // If our return type is void
        if (self->ReturnType == g.a->GetVoidType()) {
            // If we have a void-returning expression, evaluate it.
            if (ret_expr) 
                ret_expr->GetValue(g);
            destructors(g);
            g.builder.CreateRetVoid();
            return;
        }

        // If we return a simple type
        if (!self->ReturnType->IsComplexType()) {
            // and we already have an expression of that type
            if (ret_expr->GetType() == self->ReturnType) {
                // then great, just return it directly.
                auto val = ret_expr->GetValue(g);
                destructors(g);
                g.builder.CreateRet(val);
                return;
            }
            // build should be a function taking our ret's value and returning one of the type we need.
            auto val = build->GetValue(g);
            build->DestroyLocals(g);
            destructors(g);
            g.builder.CreateRet(val);
            return;
        }

        // If we return a complex type, the 0th parameter will be memory into which to place the return value.
        // build should be a function taking the memory and our ret's value and emplacing it.
        // Then return void.
        build->GetValue(g);
        build->DestroyLocals(g);
        destructors(g);
        g.builder.CreateRetVoid();
        return;
    }
};

bool is_terminated(llvm::BasicBlock* bb) {
    return !bb->empty() && bb->back().isTerminator();
}

struct Function::WhileStatement : public Statement {
    WhileStatement(std::unique_ptr<Expression> ex)
    : cond(std::move(ex)) 
    {
        ListenToNode(cond.get());
        OnNodeChanged(cond.get());
    }
    void OnNodeChanged(Node* n) override final {
        if (cond->GetType())
            boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(cond->GetType());
    }

    std::unique_ptr<Expression> cond;
    std::unique_ptr<Statement> body;
    std::function<llvm::Value*(llvm::Value*, Codegen::Generator&)> boolconvert;
    llvm::BasicBlock* continue_bb = nullptr;
    llvm::BasicBlock* check_bb = nullptr;

    void DestroyLocals(Codegen::Generator& g) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    void GenerateCode(Codegen::Generator& g) override final {
        auto&& bb = g.builder;
        check_bb = llvm::BasicBlock::Create(bb.getContext(), "check_bb", bb.GetInsertBlock()->getParent());
        auto loop_bb = llvm::BasicBlock::Create(bb.getContext(), "loop_bb", bb.GetInsertBlock()->getParent());
        continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
        bb.CreateBr(check_bb);
        bb.SetInsertPoint(check_bb);
        // Both of these branches need to destroy the cond's locals.
        // In the case of the loop, so that when check_bb is re-entered, it's clear.
        // In the case of the continue, so that the check's locals are cleaned up properly.
        bb.CreateCondBr(boolconvert(cond->GetValue(g), g), loop_bb, continue_bb);
        bb.SetInsertPoint(loop_bb);
        body->GenerateCode(g);
        body->DestroyLocals(g);
        cond->DestroyLocals(g);
        // If, for example, we unconditionally return or break/continue, it can happen that we were already terminated.
        if (!is_terminated(g.builder.GetInsertBlock()))
            bb.CreateBr(check_bb);
        bb.SetInsertPoint(continue_bb);
        cond->DestroyLocals(g);
    }
};
struct Function::IfStatement : public Statement{
    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Statement> true_b, std::unique_ptr<Statement> false_b)
    : cond(std::move(cond)), true_br(std::move(true_b)), false_br(std::move(false_b)) 
    {
        ListenToNode(cond.get());
        OnNodeChanged(cond.get());
    }
    void OnNodeChanged(Node* n) override final {
        if (cond->GetType())
            boolconvert = cond->GetType()->Decay()->BuildBooleanConversion(cond->GetType());
    }

    std::unique_ptr<Expression> cond;
    std::unique_ptr<Statement> true_br;
    std::unique_ptr<Statement> false_br;
    std::function<llvm::Value*(llvm::Value*, Codegen::Generator&)> boolconvert;

    void DestroyLocals(Codegen::Generator& g) override final {
        // After code generation, I ain't got no locals left to destroy.
    }
    void GenerateCode(Codegen::Generator& g) override final {
        auto&& bb = g.builder;
        auto true_bb = llvm::BasicBlock::Create(bb.getContext(), "true_bb", bb.GetInsertBlock()->getParent());
        auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
        auto else_bb = false_br ? llvm::BasicBlock::Create(bb.getContext(), "false_bb", bb.GetInsertBlock()->getParent()) : continue_bb;
        bb.CreateCondBr(boolconvert(cond->GetValue(g), g), true_bb, else_bb);
        bb.SetInsertPoint(true_bb);
        true_br->GenerateCode(g);
        true_br->DestroyLocals(g);
        if (!is_terminated(bb.GetInsertBlock()))
            bb.CreateBr(continue_bb);

        if (false_br) {
            bb.SetInsertPoint(else_bb);
            false_br->GenerateCode(g);
            false_br->DestroyLocals(g);
            if (!is_terminated(bb.GetInsertBlock()))
                bb.CreateBr(continue_bb);
        }

        bb.SetInsertPoint(continue_bb);
        cond->DestroyLocals(g);
    }
};
struct Function::VariableStatement : public Statement {
    VariableStatement(std::vector<LocalVariable*> locs, std::unique_ptr<Expression> expr)
    : locals(std::move(locs)), init_expr(std::move(expr)){}
    std::unique_ptr<Expression> init_expr;
    std::vector<LocalVariable*> locals;
    void DestroyLocals(Codegen::Generator& g) {
        for (auto local : locals)
            local->DestroyLocals(g);
    }
    void GenerateCode(Codegen::Generator& g) {
        for (auto local : locals)
            local->GetValue(g);
    }
};
struct Function::ConditionVariable : public Expression {
    ConditionVariable(Function::LocalVariable* expr, Analyzer& a)
    : var(std::move(expr)), analyzer(&a)
    {
        ListenToNode(var);
        OnChange();
    }
    Analyzer* analyzer;
    Function::LocalVariable* var;
    void OnNodeChanged(Node* n) {
        OnChange();
    }
    Type* GetType() {
        return var->GetType();
    }
    void DestroyLocals(Codegen::Generator& g) override final {
        var->DestroyLocals(g);
    }
    llvm::Value* ComputeValue(Codegen::Generator& g) override final {
        return var->GetValue(g);
    }
};
struct Function::VariableReference : public Expression {
    VariableReference(LocalVariable* v, Analyzer& a) {
        what = v;
        ListenToNode(what);
        OnChange();
    }
    void OnNodeChanged(Node* n) {
        OnChange();
    }
    Analyzer* analyzer;
    LocalVariable* what;
    Type* GetType() {
        return what->GetType();
    }
    void DestroyLocals(Codegen::Generator& g) override final {}
    llvm::Value* ComputeValue(Codegen::Generator& g) override final {
        return what->alloc;
    }
};
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
    std::function<void(Codegen::Generator&)> destroy_locals;
    void DestroyLocals(Codegen::Generator& g) override final {}
    void GenerateCode(Codegen::Generator& g) override final {
        destroy_locals(g);
        g.builder.CreateBr(while_stmt->continue_bb);
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
    std::function<void(Codegen::Generator&)> destroy_locals;
    void DestroyLocals(Codegen::Generator& g) override final {}
    void GenerateCode(Codegen::Generator& g) override final {
        destroy_locals(g);
        g.builder.CreateBr(while_stmt->check_bb);
    }
};
std::unique_ptr<Statement> Function::AnalyzeStatement(const AST::Statement* s) {
    if (auto ret = dynamic_cast<const AST::Return*>(s)) {
        if (ret->RetExpr) {
            return Wide::Memory::MakeUnique<ReturnStatement>(this, AnalyzeExpression(this, ret->RetExpr), current_scope);
        }
        return Wide::Memory::MakeUnique<ReturnStatement>(this, nullptr, current_scope);
    }

    if (auto comp = dynamic_cast<const AST::CompoundStatement*>(s)) {
        LocalScope compound(this);
        std::vector<std::unique_ptr<Statement>> statements;
        for (auto stmt : comp->stmts) {
            statements.push_back(AnalyzeStatement(stmt));
            compound->active.push_back(statements.back().get());
        }
        return Wide::Memory::MakeUnique<CompoundStatement>(std::move(statements));
    }

    if (auto expr = dynamic_cast<const AST::Expression*>(s))
        return AnalyzeExpression(this, expr);

    if (auto var = dynamic_cast<const AST::Variable*>(s)) {
        std::vector<LocalVariable*> locals;
        auto init_expr = AnalyzeExpression(this, var->initializer);
        if (var->name.size() == 1) {
            auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr.get());
            auto&& name = var->name.front();
            locals.push_back(var_stmt.get());
            if (current_scope->named_variables.find(var->name.front().name) != current_scope->named_variables.end())
                throw VariableShadowing(name.name, current_scope->named_variables[name.name].second, name.where);
            current_scope->named_variables[name.name] = std::make_pair(std::move(var_stmt), name.where);
        }
        unsigned i = 0;
        for (auto&& name : var->name) {
            auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr.get(), i);
            locals.push_back(var_stmt.get());
            if (current_scope->named_variables.find(name.name) != current_scope->named_variables.end())
                throw VariableShadowing(name.name, current_scope->named_variables[name.name].second, name.where);
            current_scope->named_variables[name.name] = std::make_pair(std::move(var_stmt), name.where);
        }
        return Wide::Memory::MakeUnique<VariableStatement>(std::move(locals), std::move(init_expr));
    }

    if (auto whil = dynamic_cast<const AST::While*>(s)) {
        LocalScope condscope(this);
        auto get_expr = [&, this]() -> std::unique_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                AnalyzeStatement(whil->var_condition);
                return Wide::Memory::MakeUnique<ConditionVariable>(condscope->named_variables.begin()->second.first.get());
            }
            return AnalyzeExpression(this, whil->condition);
        };
        auto cond = get_expr();
        condscope->active.push_back(cond.get());
        auto while_stmt = Wide::Memory::MakeUnique<WhileStatement>(std::move(cond));
        condscope->current_while = while_stmt.get();
        LocalScope bodyscope(this);
        auto body = AnalyzeStatement(whil->body);
        bodyscope->active.push_back(body.get());
        while_stmt->body = std::move(body);
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
                AnalyzeStatement(if_stmt->var_condition);
                return Wide::Memory::MakeUnique<ConditionVariable>(condscope->named_variables.begin()->second.first.get(), analyzer);
            }
            return AnalyzeExpression(this, if_stmt->condition);
        };
        auto cond = get_expr();
        condscope->active.push_back(cond.get());
        std::unique_ptr<Statement> true_br;
        {
            LocalScope truescope(this);
            true_br = AnalyzeStatement(if_stmt->true_statement);
        }
        std::unique_ptr<Statement> false_br;
        if (if_stmt->false_statement) {
            LocalScope falsescope(this);
            false_br = AnalyzeStatement(if_stmt->false_statement);
        }
        return Wide::Memory::MakeUnique<IfStatement>(std::move(cond), std::move(true_br), std::move(false_br));
    }

    assert(false && "Unsupported statement.");
}

struct Function::Parameter : public Expression {
    Parameter(Function* s, unsigned n)
    : self(s), num(n) 
    {
        OnNodeChanged(s);
        ListenToNode(self);
    }
    Function* self;
    unsigned num;
    std::function<void(llvm::Value*, Codegen::Generator& g)> destructor;
    Type* cur_ty = nullptr;

    void OnNodeChanged(Node* n) {
        auto get_new_ty = [this]() -> Type* {
            auto root_ty = self->Args[num];
            if (root_ty->IsReference())
                return root_ty; // Is this wrong in the case of named rvalue reference?
            return self->analyzer.GetLvalueType(root_ty);
        };
        auto new_ty = get_new_ty();
        if (new_ty != cur_ty) {
            cur_ty = new_ty;
            destructor = cur_ty->BuildDestructorCall();
            OnChange();
        }
    }


    Type* GetType() override final {
        return cur_ty;
    }
    void DestroyLocals(Codegen::Generator& g) override final {
        if (self->Args[num]->IsComplexType())
            destructor(GetValue(g), g);
    }

    llvm::Value* ComputeValue(Codegen::Generator& g) override final {
        auto argnum = num;
        if (self->ReturnType->IsComplexType())
            ++argnum;
        auto llvm_argument = std::next(self->llvmfunc->arg_begin(), argnum);

        if (self->Args[num]->IsComplexType() || self->Args[num]->IsReference())
            return llvm_argument;

        auto alloc = g.builder.CreateAlloca(self->Args[num]->GetLLVMType(g));
        alloc->setAlignment(self->Args[num]->alignment());
        g.builder.CreateStore(llvm_argument, alloc);
        return alloc;
    }
};

Function::Function(std::vector<Type*> args, const AST::FunctionBase* astfun, Analyzer& a, Type* mem, std::string src_name)
: analyzer(a)
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
        ++num; // Match up the ast args with args
        Args.push_back(args[0] == args[0]->Decay() ? a.GetLvalueType(args[0]) : args[0]);
    }
    for(auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
        auto param = Wide::Memory::MakeUnique<Parameter>(this, ++num, analyzer);
        root_scope->active.push_back(param.get());
        root_scope->named_variables[arg.name] = std::make_pair(std::move(param), arg.location);
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
            auto expr = AnalyzeExpression(this, ass->rhs);
            if (ident->val == "ExportName") {
                auto str = dynamic_cast<StringType*>(expr->GetType()->Decay());
                if (!str)
                    throw PrologExportNotAString(ass->rhs->location);
                trampoline = str->GetValue();
            }
            if (ident->val == "ReturnType") {
                auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay());
                if (!ty)
                    throw NotAType(expr->GetType()->Decay(), ass->rhs->location, a);
                ExplicitReturnType = ty->GetConstructedType();
                ReturnType = *ExplicitReturnType;
            }
        }
    }
}

Wide::Util::optional<clang::QualType> Function::GetClangType(ClangTU& where) {
    return GetSignature()->GetClangType(where);
}

struct Function::InitVTable : Statement {
    InitVTable(Function* arg)
    : self(arg) 
    {
        // Only called if it's a UDT.
        auto member = dynamic_cast<UserDefinedType*>(self->context->Decay());
        assert(member);
        init = member->SetVirtualPointers();
    }
    Function* self;
    std::function<void(llvm::Value*, Codegen::Generator& g)> init;
    virtual void DestroyLocals(Codegen::Generator& g) override final {}
    virtual void GenerateCode(Codegen::Generator& g) override final {
        auto this_arg = self->llvmfunc->arg_begin();
        init(this_arg, g);
    }
};

struct Function::InitMember : Statement {
    InitMember(Function* func, unsigned membernum, std::unique_ptr<Expression> init, Type* mem_ty)
    : self(func), num(membernum), init_expr(std::move(init)), member_ty(mem_ty) 
    {
        OnNodeChanged(init_expr.get());
    }
    Function* self;
    unsigned num;
    std::unique_ptr<Expression> init_expr;
    std::unique_ptr<Expression> inplacefunc;
    Type* member_ty;
    llvm::Value* member = nullptr;

    void OnNodeChanged(Node* n) {
        if (init_expr) {
            if (init_expr->GetType()) {
                inplacefunc = member_ty->BuildInplaceConstruction([this] { return member; }, { init_expr.get() });
            }
            return;
        }
        inplacefunc = member_ty->BuildInplaceConstruction([this] { return member; }, {});
    }
    void DestroyLocals(Codegen::Generator& g) override final {
        if (init_expr)
            init_expr->DestroyLocals(g);
        inplacefunc->DestroyLocals(g);
    }
    void GenerateCode(Codegen::Generator& g) override final {
        member = g.builder.CreateStructGEP(self->llvmfunc->arg_begin(), num);
        if (init_expr)
            inplacefunc->GetValue(g);
        else
            inplacefunc->GetValue(g);
    }
};

void Function::ComputeBody() {
    if (s == State::NotYetAnalyzed) {
        s = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor
        if (auto con = dynamic_cast<const AST::Constructor*>(fun)) {
            auto member = dynamic_cast<UserDefinedType*>(context->Decay());
            auto members = member->GetMembers();
            for (auto&& x : members) {
                // First bases, then members, then vptr.
                if (x.vptr) {
                    stmts.push_back(Wide::Memory::MakeUnique<InitVTable>(this));
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
                // For member variables, don't add them to the list, the destructor will handle them.
                if (auto init = has_initializer(x.name)) {
                    // AccessMember will automatically give us back a T*, but we need the T** here
                    // if the type of this member is a reference.
                    stmts.push_back(Wide::Memory::MakeUnique<InitMember>(this, x.num, init->initializer ? AnalyzeExpression(this, init->initializer) : nullptr, x.t));
                    continue;
                } 
                // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                if (x.InClassInitializer) {
                    stmts.push_back(Wide::Memory::MakeUnique<InitMember>(this, x.num, AnalyzeExpression(this, x.InClassInitializer), x.t));
                    continue;
                }
                stmts.push_back(Wide::Memory::MakeUnique<InitMember>(this, x.num, nullptr, x.t));
            }
            for (auto&& x : con->initializers)
                if (std::find_if(members.begin(), members.end(), [&](decltype(*members.begin())& ref) { return ref.name == x->name.front().name; }) == members.end())
                    throw NoMemberToInitialize(member, x->name.front().name, x->location, analyzer);
        }
        // Now the body.
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            stmts.push_back(AnalyzeStatement(fun->statements[i]));
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
            if (!ret->ret_expr) ret_types.insert(analyzer.GetVoidType());
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
    llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(GetSignature()->GetLLVMType(g)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, name, g.module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(g.module->getContext(), "entry", llvmfunc);
    llvm::IRBuilder<> irbuilder(bb);
    g.builder = irbuilder;
    for (auto&& stmt : stmts)
        stmt->GenerateCode(g);

    if (!is_terminated(g.builder.GetInsertBlock())) {
        if (ReturnType == analyzer.GetVoidType()) {
            g.builder.CreateRetVoid();
            current_scope->DestroyAllLocals()(g);
        }
        else
            g.builder.CreateUnreachable();
    }

    if (llvm::verifyFunction(*llvmfunc, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");

    if (trampoline) {
        // oh yay.
        // Gotta handle ABI mismatch.

        // Preserving the below code in the extremely likely case we will encounter a need for it again.
        // Check Clang's uses of this function - if it bitcasts them all we're good.
        /*for (auto use_it = f->use_begin(); use_it != f->use_end(); ++use_it) {
            auto use = *use_it;
            if (auto cast = llvm::dyn_cast<llvm::CastInst>(use)) {
                if (cast->getDestTy() != ty->getPointerTo())
                    throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
            }
            if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use)) {
                if (constant->getType() != ty->getPointerTo()) {
                    throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
                }
            }
            else
                throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
        }
        // All Clang's uses are valid.
        f->setName("__fucking__clang__type_hacks");
        auto badf = f;
        auto linkage = tramp ? llvm::GlobalValue::LinkageTypes::ExternalLinkage : llvm::GlobalValue::LinkageTypes::InternalLinkage;
        auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(Type(mod))->getElementType());
        f = llvm::Function::Create(t, linkage, name, mod);
        // Update all Clang's uses
        for (auto use_it = badf->use_begin(); use_it != badf->use_end(); ++use_it) {
            auto use = *use_it;
            if (auto cast = llvm::dyn_cast<llvm::CastInst>(use))
                cast->replaceAllUsesWith(f);
            if (auto constant = llvm::dyn_cast<llvm::ConstantExpr>(use))
                constant->replaceAllUsesWith(f);
        }*/

        auto ourret = llvmfunc->getFunctionType()->getReturnType();
        auto int8ty = llvm::IntegerType::getInt8Ty(g.module->getContext());
        auto int1ty = llvm::IntegerType::getInt1Ty(g.module->getContext());
        llvm::Type* trampret;
        llvm::Function* tramp = nullptr;
        std::vector<llvm::Value*> args;
        if (tramp = g.module->getFunction(*trampoline)) {
            trampret = tramp->getFunctionType()->getReturnType();
            if (tramp->getFunctionType() != llvmfunc->getFunctionType()) {
                // Clang's idea of the LLVM representation of this function disagrees with our own.
                // First, check the arguments.
                auto fty = tramp->getFunctionType();
                auto ty = llvmfunc->getFunctionType();
                auto arg_begin = tramp->getArgumentList().begin();
                for (std::size_t i = 0; i < fty->getNumParams(); ++i) {
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
                    if (Args[i]->size() == 0) {
                        args.push_back(llvm::Constant::getNullValue(ty->getParamType(i)));
                        continue;
                    }

                    // This code appears to just insert a null pointer for pointer/reference to eliminate type.. not sure if that's wise?!
                    /*if (auto ptr = llvm::dyn_cast<llvm::PointerType>(ty->getParamType(i))) {
                        auto el = ptr->getElementType();
                        if (g.IsEliminateType(el)) {
                            ParameterValues[i] = llvm::Constant::getNullValue(ty->getParamType(i));
                            continue;
                        }
                    }*/
                    throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
                }
                if (ourret != trampret) {
                    if (ourret != llvm::IntegerType::getInt8Ty(g.module->getContext()) || trampret != llvm::IntegerType::getInt1Ty(g.module->getContext()))
                        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
                }
            }
        } else {
            trampret = ourret;
            auto fty = llvmfunc->getFunctionType();
            if (*trampoline == "main")
                fty = llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(g.module->getContext()), false);
            tramp = llvm::Function::Create(llvmfunc->getFunctionType(), llvm::GlobalValue::ExternalLinkage, *trampoline, g.module.get());
            for (auto it = tramp->arg_begin(); it != tramp->arg_end(); ++it)
                args.push_back(&*it);
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(g.module->getContext(), "entry", tramp);
        llvm::IRBuilder<> irbuilder(bb);

        // May be ABI mismatch between ourselves and llvmfunc.
        // Consider that we may have to truncate the result, and we may have to add ret 0 for main.
        if (ReturnType == analyzer.GetVoidType()) {
            if (*trampoline == "main") {
                irbuilder.CreateCall(llvmfunc, args);
                irbuilder.CreateRet(irbuilder.getInt32(0));
            }
            irbuilder.CreateCall(llvmfunc, args);
            irbuilder.CreateRetVoid();
        } else {
            auto call = (llvm::Value*)irbuilder.CreateCall(llvmfunc, args);
            if (ourret == int8ty && trampret == int1ty)
                call = irbuilder.CreateTrunc(call, int1ty);
            irbuilder.CreateRet(call);
        }
    }
}

std::unique_ptr<Expression> Function::BuildCall(Expression* val, std::vector<Expression*> args) {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    struct Call : public Expression {
        Call(Function* s, std::vector<Expression*> args)
        : self(s), args(std::move(args)) 
        {
            ListenToNode(self);
            OnNodeChanged(self);
        }
        Function* self;
        Type* ret_ty = nullptr;
        std::vector<Expression*> args;
        std::function<void(llvm::Value*, Codegen::Generator& g)> destruct;
        void OnNodeChanged(Node* n) {
            if (self->ReturnType != ret_ty) {
                ret_ty = self->ReturnType;
                if (ret_ty->IsComplexType())
                    destruct = ret_ty->BuildDestructorCall();
                OnChange();
            }
        }
        Type* GetType() override final {
            return ret_ty;
        }
        llvm::Value* ret_temp = nullptr;
        void DestroyLocals(Codegen::Generator& g) {
            destruct(ret_temp, g);
        }
        llvm::Value* ComputeValue(Codegen::Generator& g) override final {
            // We should be protected against type mismatches by OR and the Callable interface.
            // But we must keep respecting our ABI.
            std::vector<llvm::Value*> llvmargs;
            if (ret_ty->IsComplexType())
                llvmargs.push_back(ret_temp = g.builder.CreateAlloca(ret_ty->GetLLVMType(g)));
            
            for (auto arg : args)
                llvmargs.push_back(arg->GetValue(g));
            if (!self->llvmfunc)
                self->EmitCode(g);
            
            auto call = g.builder.CreateCall(self->llvmfunc, llvmargs);
            if (ret_temp)
                return ret_temp;
            return call;
        }
    };
    return Wide::Memory::MakeUnique<Call>(this, std::move(args));
}
Wide::Util::optional<ConcreteExpression> Function::LookupLocal(std::string name) {
    Analyzer& a = *c;
    if (name == "this" && dynamic_cast<MemberFunctionContext*>(context->Decay()))
        return ConcreteExpression(c->GetLvalueType(context->Decay()), c->gen->CreateParameterExpression([this, &a] { return ReturnType->IsComplexType(a); }));
    auto expr = current_scope->LookupName(name, c);
    if (expr)
        return expr->first;
    return Util::none;
}
std::string Function::GetName() {
    return name;
}
FunctionType* Function::GetSignature() {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    if (s == State::AnalyzeInProgress)
        assert(false && "Attempted to call GetSignature whilst a function was still being analyzed.");
    return analyzer.GetFunctionType(ReturnType, Args);
}

Type* Function::GetConstantContext() {
    struct FunctionConstantContext : public MetaType {
        Type* context;
        std::unordered_map<std::string, ConcreteExpression> constant;
        Type* GetContext(Analyzer& a) override final {
            return context;
        }
        Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression e, std::string member, Context c) override final {
            if (constant.find(member) == constant.end())
                return Wide::Util::none;
            return constant.at(member).t->Decay()->GetConstantContext(*c)->BuildValueConstruction({}, c);
        }
        std::string explain(Analyzer& a) { return "function lookup context"; }
    };
    auto context = a.arena.Allocate<FunctionConstantContext>();
    context->context = GetContext(a);
    for(auto scope = current_scope; scope; scope = scope->parent) {
        for (auto&& var : scope->named_variables) {
            if (context->constant.find(var.first) == context->constant.end()) {
                auto& e = var.second.first;
                if (e.t->Decay()->GetConstantContext(a))
                    context->constant.insert(std::make_pair(var.first, e));
             }
        }
    }
    return context;
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