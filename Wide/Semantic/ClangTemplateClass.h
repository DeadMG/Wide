#pragma once

#include <Wide/Semantic/Type.h>

namespace clang {
    class ClassTemplateDecl;
}
namespace Wide {
    namespace Semantic {
        class ClangTemplateClass : public MetaType {
            clang::ClassTemplateDecl* tempdecl;
            ClangTU* from;
        public:
            ClangTemplateClass(clang::ClassTemplateDecl* decl, ClangTU* ptr)
                : tempdecl(decl), from(ptr) {}

            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression>, Context c) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}
