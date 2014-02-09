#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {
            std::vector<Type*> contents;

            std::size_t allocsize;
            std::size_t align;

            std::vector<unsigned> FieldIndices;
            std::vector<std::function<llvm::Type*(llvm::Module*)>> llvmtypes;

            bool IsComplex;
            bool copyconstructible;
            bool copyassignable;
            bool moveconstructible;
            bool moveassignable;
            bool constant;

        public:
            unsigned GetFieldIndex(unsigned num) { return FieldIndices[num]; }

            std::vector<Type*> GetMembers();
            ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a);

            AggregateType(std::vector<Type*> types, Analyzer& a);

            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            bool IsComplexType() override final;
            Type* GetConstantContext(Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Analyzer& a) override;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a) override;
            OverloadSet* CreateDestructorOverloadSet(Analyzer& a) override;

            bool IsCopyConstructible(Analyzer& a) override;
            bool IsMoveConstructible(Analyzer& a) override;
            bool IsCopyAssignable(Analyzer& a) override;
            bool IsMoveAssignable(Analyzer& a) override;
        };
    }
}