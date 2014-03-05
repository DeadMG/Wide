#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Lexer/Token.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Bool::GetLLVMType(Analyzer& a) {
    return [](llvm::Module* m) {
        return llvm::Type::getInt8Ty(m->getContext());
    };
}

Wide::Util::optional<clang::QualType> Bool::GetClangType(ClangTU& where, Analyzer& a) {
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

OverloadSet* Bool::CreateOperatorOverloadSet(Type* t, Lexer::TokenType name, Lexer::Access access, Analyzer& a) {
    if (access != Lexer::Access::Public)
        return AccessMember(t, name, Lexer::Access::Public, a);

    if (t == a.GetLvalueType(this)) {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        types.push_back(this);
        switch (name) {
        case Lexer::TokenType::OrAssign:
            return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
                auto stmt = c->gen->CreateStore(args[0].Expr, c->gen->CreateOrExpression(args[0].BuildValue(c).Expr, args[1].BuildValue(c).Expr));                
                return ConcreteExpression(args[0].t, c->gen->CreateChainExpression(stmt, args[0].Expr));
            }, types, a));
        case Lexer::TokenType::AndAssign:
            return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
                auto stmt = c->gen->CreateStore(args[0].Expr, c->gen->CreateAndExpression(args[0].BuildValue(c).Expr, args[1].BuildValue(c).Expr));
                return ConcreteExpression(args[0].t, c->gen->CreateChainExpression(stmt, args[0].Expr));
            }, types, a));
        case Lexer::TokenType::XorAssign:
            return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateXorExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
            }, types, a));
        }
        return PrimitiveType::CreateOperatorOverloadSet(t, name, access, a);
    }
    std::vector<Type*> types;
    types.push_back(this);
    types.push_back(this);
    switch(name) {
    case Lexer::TokenType::LT:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateLT(args[0].Expr, args[1].Expr, false));
        }, types, a));
    case Lexer::TokenType::EqCmp:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateEqualityExpression(args[0].Expr, args[1].Expr));
        }, types, a));
    }
    return a.GetOverloadSet();
}

std::string Bool::explain(Analyzer& a) {
    return "bool";
}