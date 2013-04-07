#pragma once

#include "Type.h"
#include <string>
#include <unordered_map>

namespace clang {
    class DeclContext;
} 

namespace Wide {
    namespace Semantic {        
        class ClangNamespace : public Type {
            clang::DeclContext* con;
            ClangUtil::ClangTU* from;
        public:
            ClangNamespace(clang::DeclContext* p, ClangUtil::ClangTU* f)
                : con(p), from(f) {}
        
            Expression AccessMember(Expression val, std::string name, Analyzer& a);
        };
    }
}