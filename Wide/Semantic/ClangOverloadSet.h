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

            ConcreteExpression BuildCallWithTemplateArguments(clang::TemplateArgumentListInfo*, ConcreteExpression mem, std::vector<ConcreteExpression>, Analyzer& a, Lexer::Range where);
        public:
            ClangOverloadSet(std::unique_ptr<clang::UnresolvedSet<8>> s, ClangUtil::ClangTU* from, Type* t = nullptr);
            
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            ConcreteExpression BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a) override;

            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;

            ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;

            using Type::BuildValueConstruction;
        };
    }
}