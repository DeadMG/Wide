#pragma once

#include <Wide/Semantic/Type.h>

#pragma warning(push, 0)

#include <clang/Sema/Overload.h>

#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        class ClangOverloadSet : public Type {
            std::unique_ptr<clang::UnresolvedSet<8>> lookupset;
            ClangUtil::ClangTU* from;
            Type* nonstatic;
            clang::TemplateArgumentListInfo* templateargs;

            Expression BuildCallWithTemplateArguments(clang::TemplateArgumentListInfo*, Expression mem, std::vector<Expression>, Analyzer& a);
        public:
            ClangOverloadSet(std::unique_ptr<clang::UnresolvedSet<8>> s, ClangUtil::ClangTU* from, Type* t = nullptr);
            
            Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) override;
            Expression BuildMetaCall(Expression val, std::vector<Expression> args, Analyzer& a) override;

            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;

            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;

            using Type::BuildValueConstruction;
        };
    }
}