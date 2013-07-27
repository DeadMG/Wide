#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)

#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>

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
Expression IntegralType::BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    if (!dynamic_cast<IntegralType*>(rhs.t)) throw std::runtime_error("Attempted to compare an integer with something that was not an integer.");
    Extend(lhs, rhs, a);
    return Expression(lhs.t, a.gen->CreateRightShift(lhs.Expr, rhs.Expr, is_signed));
}
Expression IntegralType::BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    if (!dynamic_cast<IntegralType*>(rhs.t)) throw std::runtime_error("Attempted to compare an integer with something that was not an integer.");
    Extend(lhs, rhs, a);
    return Expression(lhs.t, a.gen->CreateLeftShift(lhs.Expr, rhs.Expr));
}

void IntegralType::Extend(Expression& lhs, Expression& rhs, Analyzer& a) {
    assert(dynamic_cast<IntegralType*>(lhs.t) && dynamic_cast<IntegralType*>(rhs.t));
    auto lhsty = static_cast<IntegralType*>(lhs.t);
    auto rhsty = static_cast<IntegralType*>(rhs.t);

    auto max_bits = std::max(lhsty->bits, rhsty->bits);
    if (lhsty->bits != max_bits) {
        if (lhsty->is_signed)
            lhs = Expression(rhs.t, a.gen->CreateSignedExtension(lhs.Expr, rhsty->GetLLVMType(a)));
        else
            lhs = Expression(rhs.t, a.gen->CreateZeroExtension(lhs.Expr, rhsty->GetLLVMType(a)));
    }
    if (rhsty->bits != max_bits) {
        if (rhsty->is_signed)
            rhs = Expression(lhs.t, a.gen->CreateSignedExtension(rhs.Expr, GetLLVMType(a)));
        else
            rhs = Expression(lhs.t, a.gen->CreateZeroExtension(rhs.Expr, GetLLVMType(a)));
    }
}

Expression IntegralType::BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    auto int_type = dynamic_cast<IntegralType*>(rhs.t);
    if (!int_type) throw std::runtime_error("Attempted to compare an integer type with something that was not another integer.");
    if (is_signed != int_type->is_signed) throw std::runtime_error("Attempted to compare an integer type with a value of another integer type of different signedness.");

    Extend(lhs, rhs, a);

    if (is_signed)
        return Expression(a.GetBooleanType(), a.gen->CreateSLT(lhs.Expr, rhs.Expr));
    else
        return Expression(a.GetBooleanType(), a.gen->CreateULT(lhs.Expr, rhs.Expr));
}
Expression IntegralType::BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    if (!dynamic_cast<IntegralType*>(rhs.t)) throw std::runtime_error("Attempted to compare an integer with something that was not an integer.");
    Extend(lhs, rhs, a);
    return Expression(a.GetBooleanType(), a.gen->CreateEqualityExpression(lhs.Expr, rhs.Expr));
}

Expression IntegralType::BuildMultiply(Expression lhs, Expression rhs, Analyzer& a) {
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    if (!dynamic_cast<IntegralType*>(rhs.t)) throw std::runtime_error("Attempted to multiply an integer with something that was not an integer.");
    Extend(lhs, rhs, a);
    return Expression(this, a.gen->CreateMultiplyExpression(lhs.Expr, rhs.Expr));
}
Expression IntegralType::BuildPlus(Expression lhs, Expression rhs, Analyzer& a) {   
    lhs = lhs.BuildValue(a);
    rhs = rhs.BuildValue(a);
    if (!dynamic_cast<IntegralType*>(rhs.t)) throw std::runtime_error("Attempted to add an integer with something that was not an integer.");
    Extend(lhs, rhs, a);
    return Expression(lhs.t, a.gen->CreatePlusExpression(lhs.Expr, rhs.Expr));
}
Expression IntegralType::BuildIncrement(Expression obj, bool postfix, Analyzer& a) {    
    if (postfix) {
        if (auto lval = dynamic_cast<LvalueType*>(obj.t)) {
            auto curr = a.gen->CreateLoad(obj.Expr);
            auto next = a.gen->CreatePlusExpression(curr, a.gen->CreateIntegralExpression(1, false, GetLLVMType(a)));
            return Expression(this, a.gen->CreateChainExpression(a.gen->CreateChainExpression(curr, a.gen->CreateStore(obj.Expr, next)), curr));
        } else
            throw std::runtime_error("Attempted to postfix increment a non-lvalue integer.");
    }
    if (obj.steal || obj.t == this)
        throw std::runtime_error("Attempted to prefix increment a stealable integer.");
    auto curr = a.gen->CreateLoad(obj.Expr);
    auto next = a.gen->CreatePlusExpression(curr, a.gen->CreateIntegralExpression(1, false, GetLLVMType(a)));
    return Expression(this, a.gen->CreateChainExpression(a.gen->CreateStore(obj.Expr, next), next));
}

Codegen::Expression* IntegralType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() == 1) {
        args[0] = args[0].t->BuildValue(args[0], a);
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
        throw std::runtime_error("It is illegal to perform a signed->unsigned and widening conversion in one step.");
    }
    return a.gen->CreateStore(mem, a.gen->CreateIntegralExpression(0, false, GetLLVMType(a)));
}
std::size_t IntegralType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getTypeAllocSize(GetLLVMType(a)(&a.gen->main));
}
std::size_t IntegralType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getABIIntegerTypeAlignment(bits);
}