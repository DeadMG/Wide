#pragma once

#include <Wide/Semantic/Type.h>
#include <string>
#include <unordered_map>

namespace clang {
    class DeclContext;
} 

namespace Wide {
    namespace Semantic {        
        class ClangNamespace : public MetaType {
            clang::DeclContext* con;
            ClangTU* from;
        public:
            ClangNamespace(clang::DeclContext* p, ClangTU* f)
                : con(p), from(f) {}
        
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Context c) override;
            Type* GetContext(Analyzer& a) override;
            ClangTU* GetTU() { return from; }
        };
    }
}