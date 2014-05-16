#pragma once

#include <string>
#include <unordered_map>
#include <Wide/Semantic/Type.h>

namespace clang {
    class QualType;
    class Expr;
}

#pragma warning(push, 0)
#include <clang/Basic/Specifiers.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/AST/OperationKinds.h>
#pragma warning(pop)

namespace Wide {
    namespace Lexer {
        enum class TokenType : int;
    }
    namespace Semantic {
        struct Type;
        clang::ExprValueKind GetKindOfType(Type* t);
        class ClangTU;
        const std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>>& GetTokenMappings();
        std::unique_ptr<Expression> InterpretExpression(clang::Expr* p, ClangTU& from, Type* src);
    }
}