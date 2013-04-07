#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <string>
#include <fstream>

namespace clang {
    class QualType;
    enum ExprValueKind : int;
}

namespace Wide {
    namespace Semantic {
        struct Type;
        clang::ExprValueKind GetKindOfType(Type* t);
    }
    namespace ClangUtil {
        std::string GetDataLayoutForTriple(std::string triple);
        struct ClangTypeHasher {
            std::size_t operator()(clang::QualType t) const;
        };
    }
}