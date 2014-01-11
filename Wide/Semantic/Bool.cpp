#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Lexer/Token.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Bool::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::Type::getInt8Ty(m->getContext());
    };
}

clang::QualType Bool::GetClangType(ClangUtil::ClangTU& where, Analyzer& a) {
    return where.GetASTContext().BoolTy;
}
std::size_t Bool::size(Analyzer& a) {
    return a.gen->GetInt8AllocSize();
}
std::size_t Bool::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getABIIntegerTypeAlignment(8);
}

Codegen::Expression* Bool::BuildBooleanConversion(ConcreteExpression e, Context c) {
    return e.BuildValue(c).Expr;
}

OverloadSet* Bool::AccessMember(ConcreteExpression expr, Lexer::TokenType name, Context c) {
    if (callables.find(name) != callables.end())
        return callables[name];
    switch(name) {       
    case Lexer::TokenType::OrAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, Bool* self) {
            auto stmt = c->gen->CreateIfStatement(lhs.BuildValue(c).BuildNegate(c).Expr, c->gen->CreateStore(lhs.Expr, rhs.Expr), nullptr);
            return ConcreteExpression(lhs.t, c->gen->CreateChainExpression(stmt, lhs.Expr));
        }, this, c));
    case Lexer::TokenType::AndAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, Bool* self) {
            auto stmt = c->gen->CreateIfStatement(lhs.BuildValue(c).Expr, c->gen->CreateStore(lhs.Expr, rhs.Expr), nullptr);
            return ConcreteExpression(lhs.t, c->gen->CreateChainExpression(stmt, lhs.Expr));
        }, this, c));

    case Lexer::TokenType::XorAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, Bool* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateXorExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::LT:
        return callables[name] = c->GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, Bool* self) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateLT(lhs.Expr, rhs.Expr, false));
        }, this, c));
    case Lexer::TokenType::EqCmp:
        return callables[name] = c->GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, Bool* self) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateEqualityExpression(lhs.Expr, rhs.Expr));
        }, this, c));
    }
    return c->GetOverloadSet();
}
ConcreteExpression Bool::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c) {
    // Special-case this short-circuit.
    auto get_destructor_expression = [&] {
        auto start = (Codegen::Expression*)c->gen->CreateNop();
        for(auto var : destructors)
            start = c->gen->CreateChainExpression(start, var.AccessMember("~type", c)->BuildCall(c).Expr);
        return start;
    };
    switch(type) {
        case Lexer::TokenType::Or: {
            Codegen::Expression* var = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
            var = c->gen->CreateChainExpression(c->gen->CreateStore(var, lhs.BuildValue(c).Expr), var);
            auto result = c->gen->CreateChainExpression(c->gen->CreateIfStatement(c->gen->CreateNegateExpression(c->gen->CreateLoad(var)), c->gen->CreateStore(var, rhs.BuildValue(c).Expr), nullptr), c->gen->CreateLoad(var));
            c(ConcreteExpression(c->GetVoidType(), get_destructor_expression()));
            return ConcreteExpression(this, result);
        }
        case Lexer::TokenType::And: {
            Codegen::Expression* var = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
            var = c->gen->CreateChainExpression(c->gen->CreateStore(var, lhs.BuildValue(c).Expr), var);
            auto result = c->gen->CreateChainExpression(c->gen->CreateIfStatement(c->gen->CreateLoad(var), c->gen->CreateStore(var, rhs.BuildValue(c).Expr), nullptr), c->gen->CreateLoad(var));
            c(ConcreteExpression(c->GetVoidType(), get_destructor_expression()));
            return ConcreteExpression(this, result);
        }
    }
    return Type::BuildBinaryExpression(lhs, rhs, std::move(destructors), type, c);
}