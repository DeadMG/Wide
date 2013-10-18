#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/ClangTU.h>
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
ConcreteExpression IntegralType::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a, Lexer::Range where) {
    auto lhsval = lhs.BuildValue(a, where);
    auto rhsval = rhs.BuildValue(a, where);

    // Check that these types are valid for primitive integral operations. If not, go to ADL.
    if (lhsval.t != rhsval.t)
        return Type::BuildBinaryExpression(lhs, rhs, type, a, where);
    
    switch(type) {
    case Lexer::TokenType::LT:
        return ConcreteExpression(a.GetBooleanType(), a.gen->CreateLT(lhsval.Expr, rhsval.Expr, is_signed));
    case Lexer::TokenType::EqCmp:
        return ConcreteExpression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhsval.Expr, rhsval.Expr));
    }

    // If the LHS is not an lvalue, the assign ops are invalid, so go to ADL or default implementation.
    if (!IsLvalueType(lhs.t))
        return Type::BuildBinaryExpression(lhs, rhs, type, a, where);

    switch(type) {
    case Lexer::TokenType::RightShiftAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateRightShift(lhsval.Expr, rhsval.Expr, is_signed)));
    case Lexer::TokenType::LeftShiftAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateLeftShift(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::MulAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateMultiplyExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::PlusAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreatePlusExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::OrAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateOrExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::AndAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateAndExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::XorAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateXorExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::MinusAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateSubExpression(lhsval.Expr, rhsval.Expr)));
    case Lexer::TokenType::ModAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateModExpression(lhsval.Expr, rhsval.Expr, is_signed)));
    case Lexer::TokenType::DivAssign:
        return ConcreteExpression(lhs.t, a.gen->CreateStore(lhs.Expr, a.gen->CreateDivExpression(lhsval.Expr, rhsval.Expr, is_signed)));
    }
    
    // Not a primitive operator- report to ADL.
    return Type::BuildBinaryExpression(lhs, rhs, type, a, where);
}
ConcreteExpression IntegralType::BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a, Lexer::Range where) {    
    if (postfix) {
        if (IsLvalueType(obj.t)) {
            auto curr = a.gen->CreateLoad(obj.Expr);
            auto next = a.gen->CreatePlusExpression(curr, a.gen->CreateIntegralExpression(1, false, GetLLVMType(a)));
            return ConcreteExpression(this, a.gen->CreateChainExpression(a.gen->CreateChainExpression(curr, a.gen->CreateStore(obj.Expr, next)), curr));
        } else
            throw std::runtime_error("Attempted to postfix increment a non-lvalue integer.");
    }
    if (obj.steal || obj.t == this)
        throw std::runtime_error("Attempted to prefix increment a stealable integer.");
    auto curr = a.gen->CreateLoad(obj.Expr);
    auto next = a.gen->CreatePlusExpression(curr, a.gen->CreateIntegralExpression(1, false, GetLLVMType(a)));
    return ConcreteExpression(this, a.gen->CreateChainExpression(a.gen->CreateStore(obj.Expr, next), next));
}

Codegen::Expression* IntegralType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() == 1) {
        args[0] = args[0].BuildValue(a, where);
        if (args[0].t == this)
            return a.gen->CreateStore(mem, args[0].Expr);
        auto inttype = dynamic_cast<IntegralType*>(args[0].t);
        if (!inttype) throw std::runtime_error("Attempted to construct an integer from something that was not another integer type.");
        // If we're truncating, just truncate.
        if (bits < inttype->bits)
            return a.gen->CreateStore(mem, a.gen->CreateTruncate(args[0].Expr, GetLLVMType(a)));
        if (is_signed && inttype->is_signed)
            return a.gen->CreateStore(mem, a.gen->CreateSignedExtension(args[0].Expr, GetLLVMType(a)));
        if (!is_signed && !inttype->is_signed)
            return a.gen->CreateStore(mem, a.gen->CreateZeroExtension(args[0].Expr, GetLLVMType(a)));
        if (bits == inttype->bits)
            return a.gen->CreateStore(mem, args[0].Expr);
        throw std::runtime_error("It is illegal to perform a signed->unsigned and widening conversion in one step, even explicitly.");
    }
    if (args.size() != 0)
        throw std::runtime_error("Attempt to construct an integer from more than one argument or zero.");
    return a.gen->CreateStore(mem, a.gen->CreateIntegralExpression(0, false, GetLLVMType(a)));
}
std::size_t IntegralType::size(Analyzer& a) {
    return a.gen->GetInt8AllocSize() * (bits / 8);
}
std::size_t IntegralType::alignment(Analyzer& a) {
    return a.gen->GetDataLayout().getABIIntegerTypeAlignment(bits);
}