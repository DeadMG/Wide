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

Codegen::Expression* Bool::BuildBooleanConversion(ConcreteExpression e, Analyzer& a, Lexer::Range where) {
    return e.BuildValue(a, where).Expr;
}

OverloadSet* Bool::AccessMember(ConcreteExpression expr, Lexer::TokenType name, Analyzer& a, Lexer::Range where) {
    if (callables.find(name) != callables.end())
        return callables[name];
    switch(name) {       
    case Lexer::TokenType::OrAssign:
        return callables[name] = a.GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where, Bool* self) {
            auto stmt = a.gen->CreateIfStatement(lhs.BuildValue(a, where).BuildNegate(a, where).Expr, a.gen->CreateStore(lhs.Expr, rhs.Expr), nullptr);
            return ConcreteExpression(lhs.t, a.gen->CreateChainExpression(stmt, lhs.Expr));
        }, this, a));
    case Lexer::TokenType::AndAssign:
        return callables[name] = a.GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where, Bool* self) {
            auto stmt = a.gen->CreateIfStatement(lhs.BuildValue(a, where).Expr, a.gen->CreateStore(lhs.Expr, rhs.Expr), nullptr);
            return ConcreteExpression(lhs.t, a.gen->CreateChainExpression(stmt, lhs.Expr));
        }, this, a));

    case Lexer::TokenType::XorAssign:
        return callables[name] = a.GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where, Bool* self) {
            return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateXorExpression(a.gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, a));
    case Lexer::TokenType::LT:
        return callables[name] = a.GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where, Bool* self) {
            return ConcreteExpression(a.GetBooleanType(), a.gen->CreateLT(lhs.Expr, rhs.Expr, false));
        }, this, a));
    case Lexer::TokenType::EqCmp:
        return callables[name] = a.GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Analyzer& a, Lexer::Range where, Bool* self) {
            return ConcreteExpression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhs.Expr, rhs.Expr));
        }, this, a));
    }
    return a.GetOverloadSet();
}