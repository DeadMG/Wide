#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Semantic {
        class ArrayType : public AggregateType, public TupleInitializable {
            Type* t;
            unsigned count;
            std::vector<Type*> GetMembers() override final;
            bool HasDeclaredDynamicFunctions() override final { return false; }
            Type* GetSelfAsType() override final { return this; }
            std::unique_ptr<OverloadResolvable> IndexOperator;
        public:
            ArrayType(Analyzer& a, Type* t, unsigned num);
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::string explain() override final;
            std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final; 
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
        };
    }
}