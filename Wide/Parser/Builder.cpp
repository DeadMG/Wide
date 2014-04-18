#include <Wide/Parser/Builder.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/DebugUtilities.h>

using namespace Wide;
using namespace AST;
     
Builder::Builder(
    std::function<void(std::vector<Wide::Lexer::Range>, Parser::Error)> err, 
    std::function<void(Lexer::Range, Parser::Warning)> warn,
    std::function<void(Lexer::Range, OutliningType)> outline
)
    : GlobalModule(Lexer::Range(global_module_location), Lexer::Access::Public)
    , error(std::move(err))
    , warning(std::move(warn))
    , outlining(std::move(outline))
{
}

void RaiseError(Builder& build, Wide::Lexer::Range loc, std::vector<Wide::Lexer::Range> extra, Wide::Parser::Error what) {
    extra.insert(extra.begin(), loc);
    build.Error(std::move(extra), what);
}

Using* Builder::CreateUsing(std::string name, Lexer::Range loc, Expression* val, Module* m, Lexer::Access a) {
    auto p = arena.Allocate<Using>(val, loc, a);
    auto ret = m->decls.insert(std::make_pair(name, p));
    if (!ret.second) {
        combine_errors[m][name].insert(ret.first->second);
        combine_errors[m][name].insert(p);
    }
    return p;
}

Module* Builder::CreateModule(std::string val, Module* m, Lexer::Range l, Lexer::Access a) {
    auto p = arena.Allocate<Module>(l, a);
    if (m->decls.find(val) != m->decls.end()) {
        if (auto mod = dynamic_cast<AST::Module*>(m->decls[val])) {
            mod->where.push_back(l);
            return mod;
        } else {
            combine_errors[m][val].insert(m->decls[val]);
            combine_errors[m][val].insert(p);
        }
    } else {
        m->decls[val] = p;
    }
    return p;
}

void Builder::CreateFunction(
    std::string name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range where,
    Lexer::Range r,
    Type* p, 
    std::vector<FunctionArgument> args, 
    std::vector<Variable*> caps, 
    Lexer::Access a,
    bool dynamic
) {
    if (p->Functions.find(name) == p->Functions.end())
        p->Functions[name] = arena.Allocate<FunctionOverloadSet>(where);
    if (name == "type") {
        p->Functions[name]->functions.insert(arena.Allocate<Constructor>(std::move(body), std::move(prolog), where, std::move(args), std::move(caps), a));
        return;
    }
    for(auto var : p->variables)
        if (std::find_if(var.first->name.begin(), var.first->name.end(), [&](Variable::Name arg) { return arg.name == name; }) != var.first->name.end()) {
            std::vector<Lexer::Range> err;
            err.push_back(var.first->location);
            RaiseError(*this, where, err, Parser::Error::TypeFunctionAlreadyVariable);
        }
    p->Functions[name]->functions.insert(arena.Allocate<Function>(std::move(body), std::move(prolog), where, std::move(args), a, dynamic));
    outlining(r, OutliningType::Function);
}

void Builder::CreateFunction(
    std::string name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range where,
    Lexer::Range r,
    Module* m, 
    std::vector<FunctionArgument> args, 
    std::vector<Variable*> caps,
    Lexer::Access a,
    bool dynamic
) {
    auto func = arena.Allocate<Function>(std::move(body), std::move(prolog), where, std::move(args), a, false);   
    if (m->decls.find(name) != m->decls.end()) {
        if (auto overset = dynamic_cast<AST::FunctionOverloadSet*>(m->decls.at(name))) {
            overset->functions.insert(func);
        } else {
            auto local = arena.Allocate<AST::FunctionOverloadSet>(where);
            local->functions.insert(func);
            combine_errors[m][name].insert(m->decls[name]);
            combine_errors[m][name].insert(local);
        }
    } else {
        auto overset = arena.Allocate<AST::FunctionOverloadSet>(where);
        overset->functions.insert(func);
        m->decls[name] = overset;
    }
    outlining(r, OutliningType::Function);
}

void Builder::CreateOverloadedOperator(
    Wide::Lexer::TokenType name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range r, 
    Module* m, 
    std::vector<FunctionArgument> args,
    Lexer::Access a
) {
    if (m->opcondecls.find(name) == m->opcondecls.end())
        m->opcondecls[name] = arena.Allocate<FunctionOverloadSet>(r);
    m->opcondecls[name]->functions.insert(arena.Allocate<Function>(std::move(body), std::move(prolog), r, std::move(args), a, false));
}
void Builder::CreateOverloadedOperator(
    Wide::Lexer::TokenType name, 
    std::vector<Statement*> body, 
    std::vector<Statement*> prolog, 
    Lexer::Range r, 
    Type* t, 
    std::vector<FunctionArgument> args, 
    Lexer::Access a
) {
    if (t->opcondecls.find(name) == t->opcondecls.end())
        t->opcondecls[name] = arena.Allocate<FunctionOverloadSet>(r);
    t->opcondecls[name]->functions.insert(arena.Allocate<Function>(std::move(body), std::move(prolog), r, std::move(args), a, false));
}

Type* Builder::CreateType(std::vector<Expression*> bases, Lexer::Range loc, Lexer::Access a) { return arena.Allocate<Type>(bases, loc, a); }

Identifier* Builder::CreateIdentifier(std::string name, Lexer::Range r) 
{ return arena.Allocate<Identifier>(std::move(name), r); }

String* Builder::CreateString(std::string val, Lexer::Range r) 
{ return arena.Allocate<String>(std::move(val), r); }

MemberAccess* Builder::CreateMemberAccess(std::string mem, Expression* e, Lexer::Range r, Lexer::Range memloc)
{ return arena.Allocate<MemberAccess>(std::move(mem), e, r, memloc); }

BinaryExpression* Builder::CreateLeftShift(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LeftShift); }

FunctionCall* Builder::CreateFunctionCall(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<FunctionCall>(e, std::move(args), r); }

Return* Builder::CreateReturn(Expression* expr, Lexer::Range r) 
{ return arena.Allocate<Return>(expr, r); }

Return* Builder::CreateReturn(Lexer::Range r) 
{ return arena.Allocate<Return>(r); }

Variable* Builder::CreateVariable(std::string name, Expression* value, Lexer::Range r, Lexer::Range nameloc)
{
    std::vector<Variable::Name> names; names.push_back({ name, nameloc }); 
    return CreateVariable(std::move(names), value, r);
}

Variable* Builder::CreateVariable(std::string name, Lexer::Range r, Lexer::Range nameloc)
{ 
    return CreateVariable(std::move(name), nullptr, r, nameloc); 
}

BinaryExpression* Builder::CreateAssignment(Expression* lhs, Expression* rhs, Lexer::TokenType type) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, type); }

Integer* Builder::CreateInteger(std::string val, Lexer::Range r) 
{ return arena.Allocate<Integer>(std::move(val), r); }

BinaryExpression* Builder::CreateRightShift(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::RightShift); }

If* Builder::CreateIf(Expression* cond, Statement* true_br, Statement* false_br, Lexer::Range loc) 
{ return arena.Allocate<If>(cond, true_br, false_br, loc); }

If* Builder::CreateIf(Expression* cond, Statement* true_br, Lexer::Range loc) 
{ return CreateIf(cond, true_br, nullptr, loc); }

If* Builder::CreateIf(Variable* cond, Statement* true_br, Statement* false_br, Lexer::Range loc)
{ return arena.Allocate<If>(cond, true_br, false_br, loc); }

If* Builder::CreateIf(Variable* cond, Statement* true_br, Lexer::Range loc) 
{ return CreateIf(cond, true_br, nullptr, loc); }

CompoundStatement* Builder::CreateCompoundStatement(std::vector<Statement*> true_br, Lexer::Range loc) 
{ return arena.Allocate<CompoundStatement>(std::move(true_br), loc); }

BinaryExpression* Builder::CreateEqCmp(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::EqCmp); }

BinaryExpression* Builder::CreateNotEqCmp(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::NotEqCmp); }

MetaCall* Builder::CreateMetaFunctionCall(Expression* e, std::vector<Expression*> args, Lexer::Range r) 
{ return arena.Allocate<MetaCall>(e, std::move(args), r); }

While* Builder::CreateWhile(Expression* cond, Statement* body, Lexer::Range loc) 
{ return arena.Allocate<While>(body, cond, loc); }

This* Builder::CreateThis(Lexer::Range loc) 
{ return arena.Allocate<This>(loc); }

Lambda* Builder::CreateLambda(std::vector<FunctionArgument> args, std::vector<Statement*> body, Lexer::Range loc, bool defaultref, std::vector<Variable*> caps) 
{ return arena.Allocate<Lambda>(std::move(body), std::move(args), loc, defaultref, std::move(caps)); }

Negate* Builder::CreateNegate(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<Negate>(e, loc); }

Dereference* Builder::CreateDereference(Expression* e, Lexer::Range loc) 
{ return arena.Allocate<Dereference>(e, loc); }

PointerMemberAccess* Builder::CreatePointerAccess(std::string mem, Expression* e, Lexer::Range r, Lexer::Range memloc)
{ return arena.Allocate<PointerMemberAccess>(std::move(mem), e, r, memloc); }

BinaryExpression* Builder::CreateOr(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Or); }

BinaryExpression* Builder::CreateXor(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Xor); }

BinaryExpression* Builder::CreateAnd(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::And); }

BinaryExpression* Builder::CreateLT(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LT); }

BinaryExpression* Builder::CreateLTE(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::LTE); }

BinaryExpression* Builder::CreateGT(Expression* lhs, Expression* rhs)
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::GT); }

BinaryExpression* Builder::CreateGTE(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::GTE); }

Increment* Builder::CreatePrefixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, false); }

Increment* Builder::CreatePostfixIncrement(Expression* ex, Lexer::Range r)
{ return arena.Allocate<Increment>(ex, r, true); }

BinaryExpression* Builder::CreateAddition(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Plus); }

BinaryExpression* Builder::CreateMultiply(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Dereference); }

Auto* Builder::CreateAuto(Lexer::Range loc)
{ return arena.Allocate<Auto>(loc); }

Decrement* Builder::CreatePrefixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, false); }

Decrement* Builder::CreatePostfixDecrement(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<Decrement>(ex, r, true); }

AddressOf* Builder::CreateAddressOf(Expression* ex, Lexer::Range r) 
{ return arena.Allocate<AddressOf>(ex, r); }

std::vector<Statement*> Builder::CreateStatementGroup() 
{ return std::vector<Statement*>(); }

std::vector<Expression*> Builder::CreateExpressionGroup() 
{ return std::vector<Expression*>(); }

std::vector<Variable*> Builder::CreateCaptureGroup() 
{ return std::vector<Variable*>(); }

std::vector<Variable*> Builder::CreateInitializerGroup() 
{ return CreateCaptureGroup(); }

void Builder::SetTypeEndLocation(Lexer::Range loc, Type* t) { outlining(loc, OutliningType::Type); }
void Builder::SetModuleEndLocation(Module* m, Lexer::Range loc) { outlining(loc, OutliningType::Module); }

std::vector<FunctionArgument> Builder::CreateFunctionArgumentGroup() { return std::vector<FunctionArgument>(); }

void Builder::AddTypeField(Type* t, Variable* decl, Lexer::Access a) {
    for (auto&& name : decl->name) {
        if (t->Functions.find(name.name) != t->Functions.end()) {
            std::vector<Wide::Lexer::Range> vec;
            for (auto&& x : t->Functions[name.name]->functions)
                vec.push_back(x->where);
            throw Parser::ParserError(decl->location, vec, Parser::Error::TypeVariableAlreadyFunction);
        }
    }
    t->variables.push_back(std::make_pair(decl, a)); 
}
void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Lexer::Range r, Expression* expr)  {
    FunctionArgument arg(r);
    arg.name = name;
    arg.type = expr;
    args.push_back(arg);
}

void Builder::AddArgumentToFunctionGroup(std::vector<FunctionArgument>& args, std::string name, Lexer::Range r) { return AddArgumentToFunctionGroup(args, std::move(name), r, nullptr); }
void Builder::AddCaptureToGroup(std::vector<Variable*>& l, Variable* cap) { return l.push_back(cap); }
void Builder::AddInitializerToGroup(std::vector<Variable*>& l, Variable* b) { return AddCaptureToGroup(l, b); }
void Builder::AddStatementToGroup(std::vector<Statement*>& grp, Statement* stmt) { return grp.push_back(stmt); }
void Builder::AddExpressionToGroup(std::vector<Expression*>& grp, Expression* expr) { return grp.push_back(expr); }
void Builder::AddNameToGroup(std::vector<Variable::Name>& grp, std::string str, Lexer::Range where) { return grp.push_back({ std::move(str), where }); }

std::vector<Variable::Name> Builder::CreateVariableNameGroup() {
    return std::vector<Variable::Name>();
}

Variable* Builder::CreateVariable(std::vector<Variable::Name> names, Expression* init, Lexer::Range r) {
    if (names.size() == 0)
        Wide::Util::DebugBreak();
    return arena.Allocate<Variable>(std::move(names), init, r);
}
Lexer::Range Builder::GetLocation(Statement* s) {
    return s->location;
}

void Builder::Error(std::vector<Wide::Lexer::Range> r, Parser::Error e) {
    error(std::move(r), e);
}

BinaryExpression* Builder::CreateDivision(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Divide); }
BinaryExpression* Builder::CreateModulus(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Modulo); }
BinaryExpression* Builder::CreateSubtraction(Expression* lhs, Expression* rhs) 
{ return arena.Allocate<BinaryExpression>(lhs, rhs, Lexer::TokenType::Minus); }

void Builder::Warning(Wide::Lexer::Range where, Parser::Warning what) {
    return warning(where, what);
}

AST::ErrorExpr* Builder::CreateError(Wide::Lexer::Range where) {
    return arena.Allocate<AST::ErrorExpr>(where);
}

void Builder::Error(Wide::Lexer::Range loc, Parser::Error err) {
    std::vector<Wide::Lexer::Range> range;
    range.push_back(loc);
    Error(std::move(range), err);
}

std::vector<std::vector<std::pair<Lexer::Range, DeclContext*>>> Builder::GetCombinerErrors() {
    std::vector<std::vector<std::pair<Lexer::Range, DeclContext*>>> out;
    for(auto x : combine_errors) {
        std::unordered_map<std::string, std::vector<DeclContext*>> contexts;
        // Sort them by name.
        for (auto con : x.second)
            contexts[con.first].assign(con.second.begin(), con.second.end());
        // Issue each name as a separate list of errors.
        for(auto list : contexts) {
            out.push_back(std::vector<std::pair<Lexer::Range, DeclContext*>>());
            for(auto con : list.second)
                for(auto loc : con->where)
                    out.back().push_back(std::make_pair(loc, con));
        }
    }
    return out;
}

Break* Builder::CreateBreak(Lexer::Range r) {
    return arena.Allocate<Break>(r);
}
Continue* Builder::CreateContinue(Lexer::Range r) {
    return arena.Allocate<Continue>(r);
}

While* Builder::CreateWhile(Variable* cond, Statement* body, Lexer::Range loc) {
    return arena.Allocate<While>(body, cond, loc);
}

Tuple* Builder::CreateTuple(std::vector<Expression*> exprs, Lexer::Range where) {
    return arena.Allocate<Tuple>(std::move(exprs), where);
}
void Builder::AddTypeToModule(Wide::AST::Module* higher, std::string name, Wide::AST::Type* ty) {
    if (higher->decls.find(name) != higher->decls.end()) {
        auto con = higher->decls[name];
        combine_errors[higher][name].insert(con);
        combine_errors[higher][name].insert(ty);
    }
    else {
        higher->decls[name] = ty;
    }
}
void Builder::AddTemplateTypeToModule(Wide::AST::Module* higher, std::string name, std::vector<FunctionArgument> args, Wide::AST::Type* ty) {
    if (higher->decls.find(name) != higher->decls.end()) {
        if (auto tempoverset = dynamic_cast<Wide::AST::TemplateTypeOverloadSet*>(higher->decls.at(name))) {
            tempoverset->templatetypes.insert(arena.Allocate<Wide::AST::TemplateType>(ty, args));
            return;
        }
        auto con = higher->decls[name];
        combine_errors[higher][name].insert(con);
        combine_errors[higher][name].insert(ty);
    }
    auto set = arena.Allocate<TemplateTypeOverloadSet>(ty->where.front());
    higher->decls[name] = set;
    set->templatetypes.insert(arena.Allocate<Wide::AST::TemplateType>(ty, args));
}