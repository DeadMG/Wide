#pragma once

#include <Wide/Semantic/Type.h>

namespace clang {
    class ClassTemplateDecl;
    class ClassTemplateSpecializationDecl;
    class TemplateArgument;
}
namespace Wide {
    namespace Semantic {
        class ClangTemplateClass : public MetaType {
            clang::ClassTemplateDecl* tempdecl;
            ClangTU* from;
        public:
            ClangTemplateClass(clang::ClassTemplateDecl* decl, ClangTU* ptr, Analyzer& a)
                : tempdecl(decl), from(ptr), MetaType(a) {}

            std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::string explain() override final;
            Type* GetContext() override final;
        };
    }
}
