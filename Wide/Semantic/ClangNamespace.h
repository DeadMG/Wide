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
            ClangNamespace(clang::DeclContext* p, ClangTU* f, Analyzer& a)
                : con(p), from(f), MetaType(a) {}
        
            std::shared_ptr<Expression> AccessNamedMember(std::shared_ptr<Expression> t, std::string name, Context c) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) override final;
            ClangNamespace* GetContext() override final;
            ClangTU* GetTU() { return from; }
            std::string explain() override final;
            bool IsLookupContext() override final { return true; }
        };
    }
}