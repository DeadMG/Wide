#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/PointerType.h>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/TypeOrdering.h>
#include <clang/Frontend/CodeGenOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/ASTContext.h>
#include <clang/Lex/HeaderSearchOptions.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

clang::ExprValueKind Semantic::GetKindOfType(Type* t) {
    if (dynamic_cast<Semantic::LvalueType*>(t))
        return clang::ExprValueKind::VK_LValue;
    else 
        return clang::ExprValueKind::VK_RValue;
}


std::size_t ClangTypeHasher::operator()(clang::QualType t) const {
    return llvm::DenseMapInfo<clang::QualType>::getHashValue(t);
}   

const std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>>& Semantic::GetTokenMappings() {
    static const std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>> BinaryTokenMapping = []()
        -> std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>>
    {
        std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>> ret;
        ret[Lexer::TokenType::NotEqCmp] = std::make_pair(clang::OverloadedOperatorKind::OO_ExclaimEqual, clang::BinaryOperatorKind::BO_NE);
        ret[Lexer::TokenType::EqCmp] = std::make_pair(clang::OverloadedOperatorKind::OO_EqualEqual, clang::BinaryOperatorKind::BO_EQ);
        ret[Lexer::TokenType::LT] = std::make_pair(clang::OverloadedOperatorKind::OO_Less, clang::BinaryOperatorKind::BO_LT);
        ret[Lexer::TokenType::GT] = std::make_pair(clang::OverloadedOperatorKind::OO_Greater, clang::BinaryOperatorKind::BO_GT);
        ret[Lexer::TokenType::LTE] = std::make_pair(clang::OverloadedOperatorKind::OO_LessEqual, clang::BinaryOperatorKind::BO_LE);
        ret[Lexer::TokenType::GTE] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterEqual, clang::BinaryOperatorKind::BO_GE);
        ret[Lexer::TokenType::Assignment] = std::make_pair(clang::OverloadedOperatorKind::OO_Equal, clang::BinaryOperatorKind::BO_Assign);
        ret[Lexer::TokenType::LeftShift] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLess, clang::BinaryOperatorKind::BO_Shl);
        ret[Lexer::TokenType::LeftShiftAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLessEqual, clang::BinaryOperatorKind::BO_ShlAssign);
        ret[Lexer::TokenType::RightShift] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreater, clang::BinaryOperatorKind::BO_Shr);
        ret[Lexer::TokenType::RightShiftAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreaterEqual, clang::BinaryOperatorKind::BO_ShrAssign);
        ret[Lexer::TokenType::Plus] = std::make_pair(clang::OverloadedOperatorKind::OO_Plus, clang::BinaryOperatorKind::BO_Add);
        ret[Lexer::TokenType::PlusAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PlusEqual, clang::BinaryOperatorKind::BO_AddAssign);
        ret[Lexer::TokenType::Minus] = std::make_pair(clang::OverloadedOperatorKind::OO_Minus, clang::BinaryOperatorKind::BO_Sub);
        ret[Lexer::TokenType::MinusAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_MinusEqual, clang::BinaryOperatorKind::BO_SubAssign);
        ret[Lexer::TokenType::Divide] = std::make_pair(clang::OverloadedOperatorKind::OO_Slash, clang::BinaryOperatorKind::BO_Div);
        ret[Lexer::TokenType::DivAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_SlashEqual, clang::BinaryOperatorKind::BO_DivAssign);
        ret[Lexer::TokenType::Modulo] = std::make_pair(clang::OverloadedOperatorKind::OO_Percent, clang::BinaryOperatorKind::BO_Rem);
        ret[Lexer::TokenType::ModAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PercentEqual, clang::BinaryOperatorKind::BO_RemAssign);
        ret[Lexer::TokenType::Dereference] = std::make_pair(clang::OverloadedOperatorKind::OO_Star, clang::BinaryOperatorKind::BO_Mul);
        ret[Lexer::TokenType::MulAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_StarEqual, clang::BinaryOperatorKind::BO_MulAssign);
        ret[Lexer::TokenType::Xor] = std::make_pair(clang::OverloadedOperatorKind::OO_Caret, clang::BinaryOperatorKind::BO_Xor);
        ret[Lexer::TokenType::XorAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_CaretEqual, clang::BinaryOperatorKind::BO_XorAssign);
        ret[Lexer::TokenType::Or] = std::make_pair(clang::OverloadedOperatorKind::OO_Pipe, clang::BinaryOperatorKind::BO_Or);
        ret[Lexer::TokenType::OrAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_PipeEqual, clang::BinaryOperatorKind::BO_OrAssign);
        ret[Lexer::TokenType::And] = std::make_pair(clang::OverloadedOperatorKind::OO_Amp, clang::BinaryOperatorKind::BO_And);
        ret[Lexer::TokenType::AndAssign] = std::make_pair(clang::OverloadedOperatorKind::OO_AmpEqual, clang::BinaryOperatorKind::BO_AndAssign);
        ret[Lexer::TokenType::OpenBracket] = std::make_pair(clang::OverloadedOperatorKind::OO_Call, clang::BinaryOperatorKind::BO_Add);
        ret[Lexer::TokenType::OpenSquareBracket] = std::make_pair(clang::OverloadedOperatorKind::OO_Subscript, clang::BinaryOperatorKind::BO_Sub);
        return ret;
    }();
    return BinaryTokenMapping;
}

std::unique_ptr<Expression> Semantic::InterpretExpression(clang::Expr* expr, ClangTU& tu, Context c, Analyzer& a) {
    // Fun...
    llvm::APSInt out;
    if (expr->EvaluateAsInt(out, tu.GetASTContext())) {
        if (out.getBitWidth() == 1)
            return Wide::Memory::MakeUnique<Boolean>(out.getLimitedValue(1), a);
        auto ty = a.GetIntegralType(out.getBitWidth(), out.isSigned());
        return Wide::Memory::MakeUnique<Integer>(out, a);
    }
    if (auto binop = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
        auto lhs = InterpretExpression(binop->getLHS(), tu, c, a);
        auto rhs = InterpretExpression(binop->getRHS(), tu, c, a);
        auto code = binop->getOpcode();
        for (auto pair : GetTokenMappings()) {
            if (pair.second.second == code) {
                return lhs->GetType()->BuildBinaryExpression(std::move(lhs), std::move(rhs), pair.first, c);
            }
        }
        std::string str;
        llvm::raw_string_ostream ostr(str);
        expr->dump(ostr, tu.GetASTContext().getSourceManager());
        throw BadMacroExpression(c.where, str);
    }
    if (auto call = llvm::dyn_cast<clang::CallExpr>(expr)) {
        auto func = InterpretExpression(call->getCallee(), tu, c, a);
        std::vector<std::unique_ptr<Expression>> args;
        for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
            if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(*it))
                break;
            args.push_back(InterpretExpression(expr, tu, c, a));
        }
        return func->GetType()->BuildCall(std::move(func), std::move(args), c);
    }
    if (auto null = llvm::dyn_cast<clang::CXXNullPtrLiteralExpr>(expr)) {
        return a.GetNullType()->BuildValueConstruction(Expressions(), c);
    }
    if (auto con = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
        auto ty = a.GetClangType(tu, tu.GetASTContext().getRecordType(con->getConstructor()->getParent()));
        std::vector<std::unique_ptr<Expression>> args;
        for (auto it = con->arg_begin(); it != con->arg_end(); ++it) {
            if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(*it))
                break;
            args.push_back(InterpretExpression(*it, tu, c, a));
        }
        return ty->BuildRvalueConstruction(std::move(args), c);
    }
    if (auto paren = llvm::dyn_cast<clang::ParenExpr>(expr)) {
        return InterpretExpression(paren->getSubExpr(), tu, c, a);
    }
    if (auto str = llvm::dyn_cast<clang::StringLiteral>(expr)) {
        return Wide::Memory::MakeUnique<String>(str->getString(), a);
    }
    if (auto declref = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
        auto decl = declref->getDecl();
        // Only support function decl right now
        if (auto func = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(func);
            return a.GetOverloadSet(decls, &tu, nullptr)->BuildValueConstruction(Expressions(), c);
        }
        std::string str;
        llvm::raw_string_ostream ostr(str);
        expr->dump(ostr, tu.GetASTContext().getSourceManager());
        throw BadMacroExpression(c.where, str);
    }
    if (auto temp = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
        return InterpretExpression(temp->GetTemporaryExpr(), tu, c, a);
    }
    if (auto cast = llvm::dyn_cast<clang::ImplicitCastExpr>(expr)) {
        // C++ treats string lits as pointer to character so we need to special-case here.
        auto castty = a.GetClangType(tu, cast->getType());
        auto castexpr = InterpretExpression(cast->getSubExpr(), tu, c, a);
        if (castty == a.GetPointerType(a.GetIntegralType(8, true)) && dynamic_cast<StringType*>(castexpr->GetType()->Decay()))
            return castexpr;
        return castty->BuildRvalueConstruction(Expressions(std::move(castexpr)), c);
    }
    std::string str;
    llvm::raw_string_ostream ostr(str);
    expr->dump(ostr, tu.GetASTContext().getSourceManager());
    throw BadMacroExpression(c.where, str);
}