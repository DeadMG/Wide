#include "Builder.h"

using namespace Wide;
using namespace AST;
     
Builder::Builder()
    : GlobalModule(arena.Allocate<Module>("global"))
{
}

Return* Builder::CreateReturn(Lexer::Range r) {
    auto p =  arena.Allocate<Return>();
    p->location = r;
    return p;
}

Return* Builder::CreateReturn(Expression* e, Lexer::Range r) {
    auto p = arena.Allocate<Return>();
    p->RetExpr = std::move(e);
    p->location = r;
    return p;
}

VariableStatement* Builder::CreateVariableStatement(std::string name, Expression* value, Lexer::Range r) {
    auto p = arena.Allocate<VariableStatement>();
    p->name = std::move(name);
    p->initializer = value;
    p->location = r;
    return p;
}

AssignmentExpr* Builder::CreateAssignmentExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<AssignmentExpr>();
    p->lhs = std::move(lhs);
    p->rhs = std::move(rhs);
    p->location = lhs->GetLocation() + rhs->GetLocation();
    return p;
}

IntegerExpression* Builder::CreateIntegerExpression(std::string val, Lexer::Range r) {
    auto p = arena.Allocate<IntegerExpression>();
    p->integral_value = std::move(val);
    p->location = r;
    return p;
}

IdentifierExpr* Builder::CreateIdentExpression(std::string name, Lexer::Range r) {
    auto p = arena.Allocate<IdentifierExpr>();
    p->val = std::move(name);
    p->location = r;
    return p;
}

std::vector<Expression*> Builder::CreateExpressionGroup() { return std::vector<Expression*>(); }
std::vector<Statement*> Builder::CreateStatementGroup() { return std::vector<Statement*>(); }

StringExpr* Builder::CreateStringExpression(std::string val, Lexer::Range r) {
    auto p = arena.Allocate<StringExpr>();
    p->val = std::move(val);
    p->location = r;
    return p;
}

MemAccessExpr* Builder::CreateMemberAccessExpression(std::string mem, Expression* e, Lexer::Range r) {
    auto p = arena.Allocate<MemAccessExpr>();
    p->mem = std::move(mem);
    p->expr = std::move(e);
    p->location = r;
    return p;
}
LeftShiftExpr* Builder::CreateLeftShiftExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<LeftShiftExpr>();
    p->lhs = std::move(lhs);
    p->rhs = std::move(rhs);
    p->location = lhs->GetLocation() + rhs->GetLocation();
    return p;
}
FunctionCallExpr* Builder::CreateFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) {
    auto p = arena.Allocate<FunctionCallExpr>();
    p->args = std::move(args);
    p->callee = std::move(e);
    p->location = r;
    return p;
}

RightShiftExpr* Builder::CreateRightShiftExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<RightShiftExpr>();
    p->lhs = lhs;
    p->rhs = rhs;
    p->location = lhs->GetLocation() + rhs->GetLocation();
    return p;
}

QualifiedName* Builder::CreateQualifiedName() {
    return arena.Allocate<QualifiedName>();
}
void Builder::AddNameToQualifiedName(QualifiedName* name, std::string val) {
    name->components.push_back(val);
}

Using* Builder::CreateUsingDefinition(std::string val, Expression* name, Module* m) {
    if (m->decls.find(val) != m->decls.end())
        throw std::runtime_error("Attempted to create a using, but there was already another declaration in that slot.");
    auto p = arena.Allocate<Using>();
    p->expr = name;
    p->identifier = val;
    m->decls[val] = p;
    return p;
}
Module* Builder::CreateModule(std::string val, Module* p) {
    if(p->decls.find(val) != p->decls.end()) {
        if (auto mod = dynamic_cast<AST::Module*>(p->decls[val])) {
            return mod;
        }
        throw std::runtime_error("Attempted to create a module, but there was already another declaration in that slot.");
    }
    auto m = arena.Allocate<Module>(std::move(val));
    p->decls[m->name] = m;
    m->higher = p;
    return m;
}
Function* Builder::CreateFunction(std::string name, std::vector<Statement*> body, std::vector<Statement*> stmts, Lexer::Range r, Module* m, std::vector<Function::FunctionArgument> args) {
    auto p = arena.Allocate<Function>();
    p->name = name;
    p->statements = std::move(body);
    p->prolog = std::move(stmts);
    p->location = r;
    p->args = std::move(args);
    p->higher = m;
    if (m->decls.find(name) == m->decls.end()) {        
        auto oset = arena.Allocate<AST::FunctionOverloadSet>();
        oset->name = name;
        oset->higher = m;
        m->decls[name] = oset;
        oset->functions.push_back(p);
    } else {
        if (auto overloadset = dynamic_cast<AST::FunctionOverloadSet*>(m->decls[name])) {
            p->higher = m;
            overloadset->functions.push_back(p);
        } else
            throw std::runtime_error("Attempted to insert a function into a module, but there was already something there that was not an overload set.");
    }
    return p;
}

std::vector<Function::FunctionArgument> Builder::CreateFunctionArgumentGroup() {
    return std::vector<Function::FunctionArgument>();
}

void Builder::AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>& args, std::string name) {
    Function::FunctionArgument arg;
    arg.name = name;
    arg.type = nullptr;
    args.push_back(arg);
}

void Builder::AddArgumentToFunctionGroup(std::vector<Function::FunctionArgument>& args, std::string name, Expression* t) {
    Function::FunctionArgument arg;
    arg.name = name;
    arg.type = t;
    args.push_back(arg);
}

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br, Statement* false_br) {
    auto p = arena.Allocate<IfStatement>();
    p->condition = cond;
    p->false_statement = false_br;
    p->true_statement = true_br;
    return p;
}

IfStatement* Builder::CreateIfStatement(Expression* cond, Statement* true_br) {
    auto p = arena.Allocate<IfStatement>();
    p->condition = cond;
    p->false_statement = nullptr;
    p->true_statement = true_br;
    return p;
}

CompoundStatement* Builder::CreateCompoundStatement(std::vector<Statement*> true_br) {
    auto p = arena.Allocate<CompoundStatement>();
    p->stmts = std::move(true_br);
    return p;
}

EqCmpExpression* Builder::CreateEqCmpExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<EqCmpExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
NotEqCmpExpression* Builder::CreateNotEqCmpExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<NotEqCmpExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}

MetaCallExpr* Builder::CreateMetaFunctionCallExpression(Expression* e, std::vector<Expression*> args, Lexer::Range r) {    
    auto p = arena.Allocate<MetaCallExpr>();
    p->args = std::move(args);
    p->callee = std::move(e);
    p->location = r;
    return p;
}

WhileStatement* Builder::CreateWhileStatement(Expression* cond, Statement* body) {
    auto p = arena.Allocate<WhileStatement>();
    p->body = body;
    p->condition = cond;
    return p;
}

OrExpression* Builder::CreateOrExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<OrExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
XorExpression* Builder::CreateXorExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<XorExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
AndExpression* Builder::CreateAndExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<AndExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
LTExpression* Builder::CreateLTExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<LTExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
GTExpression* Builder::CreateGTExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<GTExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
GTEExpression* Builder::CreateGTEExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<GTEExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}
LTEExpression* Builder::CreateLTEExpression(Expression* lhs, Expression* rhs) {
    auto p = arena.Allocate<LTEExpression>();
    p->lhs = lhs;
    p->rhs = rhs;
    return p;
}