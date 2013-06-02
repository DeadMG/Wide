#pragma once

#include "MetaType.h"

namespace clang {
    class ClassTemplateDecl;
}
namespace Wide {
    namespace Semantic {
        class ClangTemplateClass : public MetaType {
            clang::ClassTemplateDecl* tempdecl;
            ClangUtil::ClangTU* from;
        public:
            using Type::BuildValueConstruction;

            ClangTemplateClass(clang::ClassTemplateDecl* decl, ClangUtil::ClangTU* ptr)
                : tempdecl(decl), from(ptr) {}
            Expression BuildMetaCall(Expression, std::vector<Expression>, Analyzer&);
        };
    }
}
