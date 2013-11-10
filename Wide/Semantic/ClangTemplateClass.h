#pragma once

#include <Wide/Semantic/Type.h>

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

            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression>, Analyzer&, Lexer::Range where) override;
        };
    }
}
