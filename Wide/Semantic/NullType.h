#pragma once

#include <Wide/Semantic/MetaType.h>

namespace Wide {
    namespace Semantic {
        struct NullType : public MetaType {    
            NullType() {}
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
        };
    }
}