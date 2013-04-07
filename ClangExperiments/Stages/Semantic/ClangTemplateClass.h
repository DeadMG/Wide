#pragma once

#include "Type.h"

namespace clang {
    class ClassTemplateDecl;
}
namespace Wide {
    namespace Semantic {
        class ClangTemplateClass : public Type {
            clang::ClassTemplateDecl* tempdecl;
            ClangUtil::ClangTU* from;
        public:
            ClangTemplateClass(clang::ClassTemplateDecl* decl, ClangUtil::ClangTU* ptr)
                : tempdecl(decl), from(ptr) {}
            Expression BuildMetaCall(Expression, std::vector<Expression>, Analyzer&);
        };
    }
}
