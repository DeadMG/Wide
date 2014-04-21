#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {

            virtual const std::vector<Type*>& GetContents() = 0;

            struct Layout {
                Layout(const std::vector<Type*>& types, Analyzer& a);

                std::size_t allocsize;
                std::size_t align;

                std::vector<unsigned> Offsets;
                std::vector<unsigned> FieldIndices;
                std::vector<std::function<llvm::Type*(llvm::Module*)>> llvmtypes;

                bool IsComplex;
                bool copyconstructible;
                bool copyassignable;
                bool moveconstructible;
                bool moveassignable;
                bool constant;
            };
            Wide::Util::optional<Layout> layout;
            Layout& GetLayout(Analyzer& a) {
                if (!layout) layout = Layout(GetContents(), a);
                return *layout;
            }
        public:
            unsigned GetFieldIndex(Analyzer& a, unsigned num) { return GetLayout(a).FieldIndices[num]; }
            unsigned GetOffset(Analyzer& a, unsigned num) { return GetLayout(a).Offsets[num]; }

            ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a);
            
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            Type* GetConstantContext(Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet(Analyzer& a);
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access, Analyzer& a) override;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) override;
            OverloadSet* CreateDestructorOverloadSet(Analyzer& a) override;

            bool IsCopyConstructible(Analyzer& a, Lexer::Access access) override;
            bool IsMoveConstructible(Analyzer& a, Lexer::Access access) override;
            bool IsCopyAssignable(Analyzer& a, Lexer::Access access) override;
            bool IsMoveAssignable(Analyzer& a, Lexer::Access access) override;
            bool IsComplexType(Analyzer& a) override;
        };
    }
}