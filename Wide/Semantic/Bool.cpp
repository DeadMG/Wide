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

clang::QualType Bool::GetClangType(ClangTU& where, Analyzer& a) {
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

OverloadSet* Bool::CreateOperatorOverloadSet(Type* t, Lexer::TokenType name, Analyzer& a) {
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
        return PrimitiveType::CreateOperatorOverloadSet(t, name, a);
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

/*#pragma warning(disable : 4715)
ConcreteExpression Bool::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c) {
    // Special-case this short-circuit.
    // Left-hand-side's destructors already been registered.
    // destructors only contains right-hand-side destructors.
    if (type != Lexer::TokenType::Or && type != Lexer::TokenType::And)
        return Type::BuildBinaryExpression(lhs, rhs, std::move(destructors), type, c);

    if (!shortcircuit_destructor_type) {
        struct des_type : public MetaType {
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression e, std::string name, Context c) final override {
                if (name == "~type") {
                    return ConcreteExpression(c->GetNothingFunctorType(), e.Expr);
                }
                return Wide::Util::none;
            }
        };
        shortcircuit_destructor_type = c->arena.Allocate<des_type>();
    }

    Codegen::Expression* var = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    auto destruct_rhs = lhs.BuildBooleanConversion(c);
    destruct_rhs = c->gen->CreateChainExpression(c->gen->CreateStore(var, destruct_rhs), destruct_rhs);

    auto register_destructor_expression = [&] {
        if (destructors.empty())
            return;
        Codegen::Statement* des = c->gen->CreateNop();
        for (auto it = destructors.rbegin(); it != destructors.rend(); ++it) {
            if (dynamic_cast<MetaType*>(it->t))
                continue;
            std::vector<Type*> types;
            types.push_back(it->t);
            des = c->gen->CreateChainStatement(des, it->t->GetDestructorOverloadSet(*c)->Resolve(types, *c)->Call(*it, c).Expr);
            des = c->gen->CreateChainStatement(des, c->gen->CreateLifetimeEnd(it->Expr));
        }
        if (dynamic_cast<Codegen::Nop*>(des))
            return;
        c(ConcreteExpression(shortcircuit_destructor_type, c->gen->CreateChainExpression(c->gen->CreateIfStatement(destruct_rhs, des, nullptr), var)));
    };

    switch(type) {
        /*
        a | b
        var := a;
        if (~var)
            var = b;
        ~a();
        if (~var)
            ~b();
        case Lexer::TokenType::Or: {
            auto result = c->gen->CreateChainExpression(c->gen->CreateIfStatement(c->gen->CreateNegateExpression(destruct_rhs), c->gen->CreateStore(var, rhs.BuildBooleanConversion(c)), nullptr), c->gen->CreateLoad(var));
            register_destructor_expression();
            return ConcreteExpression(this, result);
        }
        case Lexer::TokenType::And: {
            auto result = c->gen->CreateChainExpression(c->gen->CreateIfStatement(destruct_rhs, c->gen->CreateStore(var, rhs.BuildBooleanConversion(c)), nullptr), c->gen->CreateLoad(var));
            register_destructor_expression();
            return ConcreteExpression(this, result);
        }
    }
}*/