#pragma once
#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class Bool : public PrimitiveType {
            Type* shortcircuit_destructor_type;
        public:
            Bool() : shortcircuit_destructor_type(nullptr) {}
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            clang::QualType GetClangType(ClangTU&, Analyzer& a) override final;
            
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType name, Analyzer& a) override final;
            Codegen::Expression* BuildBooleanConversion(ConcreteExpression, Context c) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
        };
    }
}