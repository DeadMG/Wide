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
            ClangUtil::ClangTU* from;
        public:
            ClangNamespace(clang::DeclContext* p, ClangUtil::ClangTU* f)
                : con(p), from(f) {}
        
            Wide::Util::optional<Expression> AccessMember(ConcreteExpression val, std::string name, Context c) override;
            Type* GetContext(Analyzer& a) override;
            ClangUtil::ClangTU* GetTU() { return from; }
        };
    }
}