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

            std::unique_ptr<Expression> PrimitiveAccessMember(Expression* self, unsigned num);
            
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetConstantContext() override;
            llvm::Type* GetLLVMType(Codegen::Generator& g) override final;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet();
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) override;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override;

            bool IsCopyConstructible(Lexer::Access access) override;
            bool IsMoveConstructible(Lexer::Access access) override;
            bool IsCopyAssignable(Lexer::Access access) override;
            bool IsMoveAssignable(Lexer::Access access) override;
            bool IsComplexType() override;
        };
    }
}