#pragma once

#include <string>
#include <unordered_map>
#include <Wide/Semantic/Type.h>
#include <Wide/Lexer/Token.h>

namespace clang {
    class QualType;
    class Expr;
    class FunctionDecl;
}

#pragma warning(push, 0)
#include <clang/Basic/Specifiers.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/AST/OperationKinds.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        struct Type;
        class FunctionType;
        clang::ExprValueKind GetKindOfType(Type* t);
        class ClangTU;
        const std::unordered_map<Parse::OperatorName, std::pair<clang::OverloadedOperatorKind, Wide::Util::optional<clang::BinaryOperatorKind>>>& GetTokenMappings();
        std::shared_ptr<Expression> InterpretExpression(clang::Expr* p, ClangTU& from, Context c, Analyzer& a);
        std::shared_ptr<Expression> InterpretExpression(clang::Expr* p, ClangTU& from, Context c, Analyzer& a, std::unordered_map<clang::Expr*, std::shared_ptr<Expression>> exprmap);
        clang::CallingConv GetCallingConvention(clang::FunctionDecl* decl);
        FunctionType* GetFunctionType(clang::FunctionDecl* decl, ClangTU& from, Analyzer& a);
    }
}