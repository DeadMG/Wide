#pragma once
#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class Bool : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> OrAssignOperator;
            std::unique_ptr<OverloadResolvable> XorAssignOperator;
            std::unique_ptr<OverloadResolvable> AndAssignOperator;
            std::unique_ptr<OverloadResolvable> LTOperator;
            std::unique_ptr<OverloadResolvable> EQOperator;
            std::unique_ptr<OverloadResolvable> NegOperator;
        public:
            Bool(Analyzer& a) : PrimitiveType(a) {}
            llvm::Type* GetLLVMType(Codegen::Generator& g) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU&) override final;
            
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType name, Lexer::Access access) override final;
            std::unique_ptr<Expression> BuildBooleanConversion(std::unique_ptr<Expression>, Context c) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
        };
    }
}