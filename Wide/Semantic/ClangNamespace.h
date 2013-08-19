#pragma once

#include <Wide/Semantic/MetaType.h>
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
        
            Wide::Util::optional<Expression> AccessMember(Expression val, std::string name, Analyzer& a) override;
        };
    }
}