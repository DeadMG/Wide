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

const std::unordered_map<Parse::OperatorName, std::pair<clang::OverloadedOperatorKind, Wide::Util::optional<clang::BinaryOperatorKind>>>& Semantic::GetTokenMappings() {
    static const std::unordered_map<Parse::OperatorName, std::pair<clang::OverloadedOperatorKind, Wide::Util::optional<clang::BinaryOperatorKind>>> BinaryTokenMapping = []()
        -> std::unordered_map<Parse::OperatorName, std::pair<clang::OverloadedOperatorKind, Wide::Util::optional<clang::BinaryOperatorKind>>>
    {
        std::unordered_map<Parse::OperatorName, std::pair<clang::OverloadedOperatorKind, Wide::Util::optional<clang::BinaryOperatorKind>>> ret;
        ret[{ &Lexer::TokenTypes::Increment}] = std::make_pair(clang::OverloadedOperatorKind::OO_PlusPlus, Wide::Util::none);
        ret[{ &Lexer::TokenTypes::Decrement}] = std::make_pair(clang::OverloadedOperatorKind::OO_MinusMinus, Wide::Util::none);
        ret[{ &Lexer::TokenTypes::NotEqCmp }] = std::make_pair(clang::OverloadedOperatorKind::OO_ExclaimEqual, clang::BinaryOperatorKind::BO_NE);
        ret[{ &Lexer::TokenTypes::EqCmp }] = std::make_pair(clang::OverloadedOperatorKind::OO_EqualEqual, clang::BinaryOperatorKind::BO_EQ);
        ret[{ &Lexer::TokenTypes::LT }] = std::make_pair(clang::OverloadedOperatorKind::OO_Less, clang::BinaryOperatorKind::BO_LT);
        ret[{ &Lexer::TokenTypes::GT }] = std::make_pair(clang::OverloadedOperatorKind::OO_Greater, clang::BinaryOperatorKind::BO_GT);
        ret[{ &Lexer::TokenTypes::LTE }] = std::make_pair(clang::OverloadedOperatorKind::OO_LessEqual, clang::BinaryOperatorKind::BO_LE);
        ret[{ &Lexer::TokenTypes::GTE }] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterEqual, clang::BinaryOperatorKind::BO_GE);
        ret[{ &Lexer::TokenTypes::Assignment }] = std::make_pair(clang::OverloadedOperatorKind::OO_Equal, clang::BinaryOperatorKind::BO_Assign);
        ret[{ &Lexer::TokenTypes::LeftShift }] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLess, clang::BinaryOperatorKind::BO_Shl);
        ret[{ &Lexer::TokenTypes::LeftShiftAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_LessLessEqual, clang::BinaryOperatorKind::BO_ShlAssign);
        ret[{ &Lexer::TokenTypes::RightShift }] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreater, clang::BinaryOperatorKind::BO_Shr);
        ret[{ &Lexer::TokenTypes::RightShiftAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_GreaterGreaterEqual, clang::BinaryOperatorKind::BO_ShrAssign);
        ret[{ &Lexer::TokenTypes::Plus }] = std::make_pair(clang::OverloadedOperatorKind::OO_Plus, clang::BinaryOperatorKind::BO_Add);
        ret[{ &Lexer::TokenTypes::PlusAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_PlusEqual, clang::BinaryOperatorKind::BO_AddAssign);
        ret[{ &Lexer::TokenTypes::Minus }] = std::make_pair(clang::OverloadedOperatorKind::OO_Minus, clang::BinaryOperatorKind::BO_Sub);
        ret[{ &Lexer::TokenTypes::MinusAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_MinusEqual, clang::BinaryOperatorKind::BO_SubAssign);
        ret[{ &Lexer::TokenTypes::Divide }] = std::make_pair(clang::OverloadedOperatorKind::OO_Slash, clang::BinaryOperatorKind::BO_Div);
        ret[{ &Lexer::TokenTypes::DivAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_SlashEqual, clang::BinaryOperatorKind::BO_DivAssign);
        ret[{ &Lexer::TokenTypes::Modulo }] = std::make_pair(clang::OverloadedOperatorKind::OO_Percent, clang::BinaryOperatorKind::BO_Rem);
        ret[{ &Lexer::TokenTypes::ModAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_PercentEqual, clang::BinaryOperatorKind::BO_RemAssign);
        ret[{ &Lexer::TokenTypes::Star }] = std::make_pair(clang::OverloadedOperatorKind::OO_Star, clang::BinaryOperatorKind::BO_Mul);
        ret[{ &Lexer::TokenTypes::MulAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_StarEqual, clang::BinaryOperatorKind::BO_MulAssign);
        ret[{ &Lexer::TokenTypes::Xor }] = std::make_pair(clang::OverloadedOperatorKind::OO_Caret, clang::BinaryOperatorKind::BO_Xor);
        ret[{ &Lexer::TokenTypes::XorAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_CaretEqual, clang::BinaryOperatorKind::BO_XorAssign);
        ret[{ &Lexer::TokenTypes::Or }] = std::make_pair(clang::OverloadedOperatorKind::OO_Pipe, clang::BinaryOperatorKind::BO_Or);
        ret[{ &Lexer::TokenTypes::OrAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_PipeEqual, clang::BinaryOperatorKind::BO_OrAssign);
        ret[{ &Lexer::TokenTypes::And }] = std::make_pair(clang::OverloadedOperatorKind::OO_Amp, clang::BinaryOperatorKind::BO_And);
        ret[{ &Lexer::TokenTypes::AndAssign }] = std::make_pair(clang::OverloadedOperatorKind::OO_AmpEqual, clang::BinaryOperatorKind::BO_AndAssign);
        ret[{ &Lexer::TokenTypes::OpenBracket, &Lexer::TokenTypes::CloseBracket }] = std::make_pair(clang::OverloadedOperatorKind::OO_Call, Wide::Util::none);
        ret[{ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::CloseSquareBracket }] = std::make_pair(clang::OverloadedOperatorKind::OO_Subscript, Wide::Util::none);
        return ret;
    }();
    return BinaryTokenMapping;
}

std::shared_ptr<Expression> Semantic::InterpretExpression(clang::Expr* expr, ClangTU& tu, Context c, Analyzer& a, std::unordered_map<clang::Expr*, std::shared_ptr<Expression>> exprmap) {
    if (exprmap.find(expr) != exprmap.end())
        return exprmap[expr];
    llvm::APSInt out;
    if (expr->EvaluateAsInt(out, tu.GetASTContext())) {
        if (out.getBitWidth() == 1)
            return Wide::Memory::MakeUnique<Boolean>(out.getLimitedValue(1), a);
        auto ty = a.GetIntegralType(out.getBitWidth(), out.isSigned());
        return Wide::Memory::MakeUnique<Integer>(out, a);
    }
    if (auto binop = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
        auto lhs = InterpretExpression(binop->getLHS(), tu, c, a, exprmap);
        auto rhs = InterpretExpression(binop->getRHS(), tu, c, a, exprmap);
        auto code = binop->getOpcode();
        for (auto pair : GetTokenMappings()) {
            if (*pair.second.second == code) {
                return Type::BuildBinaryExpression(Expression::NoInstance(), std::move(lhs), std::move(rhs), pair.first.front(), c);
            }
        }
        std::string str;
        llvm::raw_string_ostream ostr(str);
        expr->dump(ostr, tu.GetASTContext().getSourceManager());
        throw SpecificError<UnsupportedBinaryExpression>(a, c.where, str);
    }
    if (auto call = llvm::dyn_cast<clang::CallExpr>(expr)) {
        auto func = InterpretExpression(call->getCallee(), tu, c, a, exprmap);
        std::vector<std::shared_ptr<Expression>> args;
        for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
            if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(*it))
                break;
            args.push_back(InterpretExpression(expr, tu, c, a, exprmap));
        }
        return Type::BuildCall(Expression::NoInstance(), std::move(func), std::move(args), c);
    }
    if (auto null = llvm::dyn_cast<clang::CXXNullPtrLiteralExpr>(expr)) {
        return a.GetNullType()->BuildValueConstruction(Expression::NoInstance(), {}, c);
    }
    if (auto con = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
        auto ty = a.GetClangType(tu, tu.GetASTContext().getRecordType(con->getConstructor()->getParent()));
        std::vector<std::shared_ptr<Expression>> args;
        for (auto it = con->arg_begin(); it != con->arg_end(); ++it) {
            if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(*it))
                break;
            args.push_back(InterpretExpression(*it, tu, c, a, exprmap));
        }
        return ty->BuildRvalueConstruction(Expression::NoInstance(), std::move(args), c);
    }
    if (auto paren = llvm::dyn_cast<clang::ParenExpr>(expr)) {
        return InterpretExpression(paren->getSubExpr(), tu, c, a, exprmap);
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
            return a.GetOverloadSet(decls, &tu, nullptr)->BuildValueConstruction(Expression::NoInstance(), {}, c);
        }
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            auto var = tu.GetObject(a, vardecl);
            return CreatePrimGlobal(Range::Empty(), a.GetLvalueType(a.GetClangType(tu, vardecl->getType())), [var](CodegenContext& con) {
                return var(con);
            });
        }
        std::string str;
        llvm::raw_string_ostream ostr(str);
        expr->dump(ostr, tu.GetASTContext().getSourceManager());
        throw SpecificError<UnsupportedDeclarationExpression>(a, c.where, str);
    }
    if (auto temp = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
        return InterpretExpression(temp->GetTemporaryExpr(), tu, c, a, exprmap);
    }
    if (auto cast = llvm::dyn_cast<clang::ImplicitCastExpr>(expr)) {
        // C++ treats string lits as pointer to character so we need to special-case here.
        auto castty = a.GetClangType(tu, cast->getType());
        auto castexpr = InterpretExpression(cast->getSubExpr(), tu, c, a, exprmap);
        if (castty == a.GetPointerType(a.GetIntegralType(8, true)) && dynamic_cast<StringType*>(castexpr->GetType(Expression::NoInstance())->Decay()))
            return castexpr;
        if (castty == a.GetBooleanType()) {
            return Type::BuildBooleanConversion(Expression::NoInstance(), castexpr, c);
        }
        return castty->BuildRvalueConstruction(Expression::NoInstance(), { castexpr }, c);
    }
    if (auto mem = llvm::dyn_cast<clang::MemberExpr>(expr)) {
        auto object = InterpretExpression(mem->getBase(), tu, c, a, exprmap);
        auto decl = mem->getMemberDecl();
        auto name = mem->getMemberNameInfo().getAsString();
        if (auto vardecl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            return Type::AccessMember(Expression::NoInstance(), object, name, c);
        }
        if (auto convdecl = llvm::dyn_cast<clang::CXXConversionDecl>(decl)) {
            std::unordered_set<clang::NamedDecl*> decls;
            decls.insert(convdecl);
            return a.GetOverloadSet(decls, &tu, object->GetType(Expression::NoInstance()))->BuildValueConstruction(Expression::NoInstance(), { object }, c);
        }
        if (auto funcdecl = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
            return Type::AccessMember(Expression::NoInstance(), object, name, c);
        }
        // foauck.
    }
    if (auto bindtemp = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr)) {
        return InterpretExpression(bindtemp->getSubExpr(), tu, c, a, exprmap);
    }

    std::string str;
    llvm::raw_string_ostream ostr(str);
    expr->dump(ostr, tu.GetASTContext().getSourceManager());
    ostr.flush();
    throw SpecificError<UnsupportedMacroExpression>(a, c.where, str);
}
std::shared_ptr<Expression> Semantic::InterpretExpression(clang::Expr* expr, ClangTU& tu, Context c, Analyzer& a) {
    return InterpretExpression(expr, tu, c, a, std::unordered_map<clang::Expr*, std::shared_ptr<Expression>>());
}
clang::CallingConv Semantic::GetCallingConvention(clang::FunctionDecl* decl) {
    return decl->getType()->getAs<clang::FunctionProtoType>()->getCallConv();
}
ClangFunctionType* Semantic::GetFunctionType(clang::FunctionDecl* decl, ClangTU& from, Analyzer& a) {
    auto prototy = decl->getType()->getAs<clang::FunctionProtoType>();
    Wide::Util::optional<clang::QualType> self;
    if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
        if (!meth->isStatic()) {
            if (prototy->getExtProtoInfo().RefQualifier == clang::RefQualifierKind::RQ_RValue)
                self = from.GetASTContext().getRValueReferenceType(from.GetASTContext().getRecordType(meth->getParent()));
            else
                self = from.GetASTContext().getLValueReferenceType(from.GetASTContext().getRecordType(meth->getParent()));
        }
    }
    return a.GetFunctionType(prototy, self, from);
}