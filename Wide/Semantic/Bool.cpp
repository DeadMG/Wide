#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
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

Codegen::Expression* Bool::BuildBooleanConversion(Expression e, Analyzer& a) {
    return e.BuildValue(a).Expr;
}

std::size_t Bool::size(Analyzer& a) {
    return a.gen->GetInt8AllocSize();
}
std::size_t Bool::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getABIIntegerTypeAlignment(8);
}

Expression Bool::BuildBinaryExpression(Expression lhs, Expression rhs, Wide::Lexer::TokenType type, Analyzer& a) {
    auto lhsval = lhs.BuildValue(a);
    auto rhsval = rhs.BuildValue(a);

    // If the types are not suitable for primitive ops, fall back to ADL.
    if (lhs.t->Decay() != this || rhs.t->Decay() != this)
        return Type::BuildBinaryExpression(lhs, rhs, type, a);

    switch(type) {
    case Lexer::TokenType::EqCmp:
        return Expression(this, a.gen->CreateEqualityExpression(lhsval.Expr, rhsval.Expr));
        // Let the default come for ~=.
    }

    if (!a.IsLvalueType(lhs.t))
        return Type::BuildBinaryExpression(lhs, rhs, type, a);
    
    switch(type) {
    case Lexer::TokenType::AndAssign:
        return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateAndExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::XorAssign:
        return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateXorExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::OrAssign:
        return Expression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateOrExpression(lhsval.Expr, rhsval.Expr)));
    }
    return Type::BuildBinaryExpression(lhs, rhs, type, a);
}