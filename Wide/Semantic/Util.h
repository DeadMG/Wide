#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <string>

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
        struct ClangTypeHasher {
            std::size_t operator()(clang::QualType t) const;
        };
    }
}