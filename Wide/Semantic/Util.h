#pragma once

#include <string>
#include <unordered_map>

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
        struct Context;
        struct ConcreteExpression;    
        clang::ExprValueKind GetKindOfType(Type* t);
        class ClangTU;
        const std::unordered_map<Lexer::TokenType, std::pair<clang::OverloadedOperatorKind, clang::BinaryOperatorKind>>& GetTokenMappings();
        ConcreteExpression InterpretExpression(clang::Expr* p, ClangTU& from, Context c);
    }
}