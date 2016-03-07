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
            Location l;
        public:
            ClangTemplateClass(clang::ClassTemplateDecl* decl, ClangTU* ptr, Location l, Analyzer& a)
                : tempdecl(decl), from(ptr), l(std::move(l)), MetaType(a) {}

            std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::string explain() override final;
        };
    }
}
