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

using namespace Wide;
using namespace Semantic;

clang::QualType IntegralType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
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
ConcreteExpression IntegralType::BuildIncrement(ConcreteExpression obj, bool postfix,Context c) {    
    if (postfix) {
        if (IsLvalueType(obj.t)) {
            auto curr = c->gen->CreateLoad(obj.Expr);
            auto next = c->gen->CreatePlusExpression(curr, c->gen->CreateIntegralExpression(1, false, GetLLVMType(*c)));
            return ConcreteExpression(this, c->gen->CreateChainExpression(c->gen->CreateChainExpression(curr, c->gen->CreateStore(obj.Expr, next)), curr));
        } else
            throw std::runtime_error("Attempted to postfix increment a non-lvalue integer.");
    }
    if (obj.steal || obj.t == this)
        throw std::runtime_error("Attempted to prefix increment a stealable integer.");
    auto curr = c->gen->CreateLoad(obj.Expr);
    auto next = c->gen->CreatePlusExpression(curr, c->gen->CreateIntegralExpression(1, false, GetLLVMType(*c)));
    return ConcreteExpression(this, c->gen->CreateChainExpression(c->gen->CreateStore(obj.Expr, next), next));
}

Codegen::Expression* IntegralType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args,Context c) {
    if (args.size() == 1) {
        args[0] = args[0].BuildValue(c);
        if (args[0].t == this)
            return c->gen->CreateStore(mem, args[0].Expr);
        auto inttype = dynamic_cast<IntegralType*>(args[0].t);
        if (!inttype) throw std::runtime_error("Attempted to construct an integer from something that was not another integer type.");
        // If we're truncating, just truncate.
        if (bits < inttype->bits)
            return c->gen->CreateStore(mem, c->gen->CreateTruncate(args[0].Expr, GetLLVMType(*c)));
        if (is_signed && inttype->is_signed)
            return c->gen->CreateStore(mem, c->gen->CreateSignedExtension(args[0].Expr, GetLLVMType(*c)));
        if (!is_signed && !inttype->is_signed)
            return c->gen->CreateStore(mem, c->gen->CreateZeroExtension(args[0].Expr, GetLLVMType(*c)));
        if (bits == inttype->bits)
            return c->gen->CreateStore(mem, args[0].Expr);
        throw std::runtime_error("It is illegal to perform a signed->unsigned and widening conversion in one step, even explicitly.");
    }
    if (args.size() != 0)
        throw std::runtime_error("Attempt to construct an integer from more than one argument or zero.");
    return c->gen->CreateStore(mem, c->gen->CreateIntegralExpression(0, false, GetLLVMType(*c)));
}
std::size_t IntegralType::size(Analyzer& a) {
    return a.gen->GetInt8AllocSize() * (bits / 8);
}
std::size_t IntegralType::alignment(Analyzer& a) {
    return a.gen->GetDataLayout().getABIIntegerTypeAlignment(bits);
}
OverloadSet* IntegralType::PerformADL(Lexer::TokenType name, Type* lhs, Type* rhs, Context c) {
    if (callables.find(name) != callables.end())
        return callables[name];
    switch(name) {
    case Lexer::TokenType::RightShiftAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateRightShift(c->gen->CreateLoad(lhs.Expr), rhs.Expr, self->is_signed)));
        }, this, c));
    case Lexer::TokenType::LeftShiftAssign:  
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateLeftShift(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::MulAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateMultiplyExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::PlusAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreatePlusExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));        
    case Lexer::TokenType::OrAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateOrExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::AndAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateAndExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::XorAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateXorExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::MinusAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateSubExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr)));
        }, this, c));
    case Lexer::TokenType::ModAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateModExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr, self->is_signed)));
        }, this, c));
    case Lexer::TokenType::DivAssign:
        return callables[name] = c->GetOverloadSet(make_assignment_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, c->gen->CreateDivExpression(c->gen->CreateLoad(lhs.Expr), rhs.Expr, self->is_signed)));
        }, this, c));
    case Lexer::TokenType::LT:
        return callables[name] = c->GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateLT(lhs.Expr, rhs.Expr, self->is_signed));
        }, this, c));
    case Lexer::TokenType::EqCmp:
        return callables[name] = c->GetOverloadSet(make_value_callable([](ConcreteExpression lhs, ConcreteExpression rhs, Context c, IntegralType* self) {
            return ConcreteExpression(c->GetBooleanType(), c->gen->CreateEqualityExpression(lhs.Expr, rhs.Expr));
        }, this, c));
    }
    return c->GetOverloadSet();
}
bool IntegralType::IsA(Type* other) {
    if (this == other) return true;
    auto otherint = dynamic_cast<IntegralType*>(other->Decay());
    if (!otherint)
        return false;
    if (is_signed == otherint->is_signed && otherint->bits > bits)
        return true;
    if (!is_signed && otherint->is_signed && otherint->bits > bits)
        return true;
    return false;
}