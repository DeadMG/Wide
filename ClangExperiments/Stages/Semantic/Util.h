#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <string>
#include <fstream>

namespace clang {
    class QualType;
#if _MSC_VER
    enum ExprValueKind : int;
#else
}
#include <clang/Basic/Specifiers.h>
namespace clang {
#endif
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