#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

clang::QualType IntegralType::GetClangType(ClangTU& TU, Analyzer& a) {
    switch(bits) {
    case 8:
        if (is_signed)
            return TU.GetASTContext().CharTy;
        else
            return TU.GetASTContext().UnsignedCharTy;
    case 16:
        if (is_signed)
            return TU.GetASTContext().ShortTy;
        else
            return TU.GetASTContext().UnsignedShortTy;
    case 32:
        if (is_signed)
            return TU.GetASTContext().IntTy;
        else
            return TU.GetASTContext().UnsignedIntTy;
    case 64:
        if (is_signed)
            return TU.GetASTContext().LongLongTy;
        else
            return TU.GetASTContext().UnsignedLongLongTy;
    }
    throw std::runtime_error("An integral type whose width was not 8, 16, 32, or 64? dafuq?");
}
std::function<llvm::Type*(llvm::Module*)> IntegralType::GetLLVMType(Analyzer& a) {
    return [this](llvm::Module* m) {
        return llvm::IntegerType::get(m->getContext(), bits);
    };
}

OverloadSet* IntegralType::CreateConstructorOverloadSet(Wide::Semantic::Analyzer& a) {    
    struct integral_constructor : public OverloadResolvable, Callable {

        integral_constructor(IntegralType* self)
            : integral(self) {}

        IntegralType* integral;
        unsigned GetArgumentCount() override final { return 2; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final {
            if (num == 0 && t == a.GetLvalueType(integral)) return t;
            if (num == 1)
                if (auto intty = dynamic_cast<IntegralType*>(t->Decay()))
                    if (intty->bits > integral->bits && intty->is_signed != integral->is_signed)
                        return nullptr;
                    else
                        return t;
            return nullptr;
        }
        Callable* GetCallableForResolution(std::vector<Type*> types, Analyzer& a) override final { return this; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            args[1] = args[1].BuildValue(c);
            if (args[1].t == integral)
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
            auto inttype = dynamic_cast<IntegralType*>(args[1].t);
            if (integral->bits < inttype->bits)
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateTruncate(args[1].Expr, integral->GetLLVMType(*c))));
            if (integral->is_signed && inttype->is_signed)
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateSignedExtension(args[1].Expr, integral->GetLLVMType(*c))));
            if (!integral->is_signed && !inttype->is_signed)
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateZeroExtension(args[1].Expr, integral->GetLLVMType(*c))));
            if (integral->bits == inttype->bits)
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
            assert(false && "Integer constructor called with conditions that OR should have prevented.");
            return ConcreteExpression(nullptr, nullptr);// shush warning
        }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final {
            return args;
        }
    };
    return a.GetOverloadSet(a.arena.Allocate<integral_constructor>(this));
}

std::size_t IntegralType::size(Analyzer& a) {
    return a.gen->GetInt8AllocSize() * (bits / 8);
}
std::size_t IntegralType::alignment(Analyzer& a) {
    return a.gen->GetDataLayout().getABIIntegerTypeAlignment(bits);
}
OverloadSet* IntegralType::CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(this);
    switch (name) {
    case Lexer::TokenType::RightShiftAssign:
        return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateRightShift(c->gen->CreateLoad(args[0].Expr), args[1].Expr, is_signed)));
        }, types, a));
    case Lexer::TokenType::LeftShiftAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateLeftShift(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::MulAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateMultiplyExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::PlusAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreatePlusExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::OrAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateOrExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::AndAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateAndExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::XorAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateXorExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::MinusAssign:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateSubExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr)));
        }, types, a));
    case Lexer::TokenType::ModAssign:
        return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateModExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr, is_signed)));
        }, types, a));
    case Lexer::TokenType::DivAssign:
        return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateDivExpression(c->gen->CreateLoad(args[0].Expr), args[1].Expr, is_signed)));
        }, types, a));
    }
    types[0] = this;
    switch(name) {
    case Lexer::TokenType::LT:
        return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateLT(args[0].Expr, args[1].Expr, is_signed));
        }, types, a));
    case Lexer::TokenType::EqCmp:
        return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateEqualityExpression(args[0].Expr, args[1].Expr));
        }, types, a));
    }
    return PrimitiveType::CreateADLOverloadSet(name, lhs, rhs, a);
}
bool IntegralType::IsA(Type* self, Type* other, Analyzer& a) {
    // If we already are, then don't bother.
    if (Type::IsA(self, other, a)) return true;

    // T to U conversion
    // Cannot be U&
    if (IsLvalueType(other)) return false;

    auto otherint = dynamic_cast<IntegralType*>(other->Decay());
    if (!otherint)
        return false;

    if (is_signed == otherint->is_signed && otherint->bits > bits)
        return true;
    if (!is_signed && otherint->is_signed && otherint->bits > bits)
        return true;
    return false;
}
OverloadSet* IntegralType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Analyzer& a) {
    switch (what) {
    case Lexer::TokenType::Increment:
        return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            auto curr = c->gen->CreateLoad(args[0].Expr);
            auto next = c->gen->CreatePlusExpression(curr, c->gen->CreateIntegralExpression(1, false, GetLLVMType(*c)));
            return ConcreteExpression(this, c->gen->CreateStore(args[0].Expr, next));
        }, { a.GetLvalueType(this) }, a));
    }
    return PrimitiveType::CreateOperatorOverloadSet(self, what, a);
}