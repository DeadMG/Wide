#pragma once
#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class Bool : public Type {
            std::unordered_map<Lexer::TokenType, OverloadSet*> callables;
        public:
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            clang::QualType GetClangType(ClangUtil::ClangTU&, Analyzer& a) override;
            
            OverloadSet* AccessMember(ConcreteExpression expr, Lexer::TokenType name, Context c) override;
            Codegen::Expression* BuildBooleanConversion(ConcreteExpression, Context c) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}