#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {

            virtual const std::vector<Type*>& GetContents() = 0;

            struct Layout {
                struct CodeGen {
                    CodeGen(AggregateType* self, Layout& lay, Codegen::Generator& g);
                    bool IsComplex;
                    llvm::Type* llvmtype;
                };
                Layout(const std::vector<Type*>& types, Analyzer& a);

                std::size_t allocsize;
                std::size_t align;

                std::vector<unsigned> Offsets;
                std::vector<unsigned> FieldIndices;
                std::vector<std::function<llvm::Type*(Codegen::Generator& g)>> llvmtypes;

                bool copyconstructible;
                bool copyassignable;
                bool moveconstructible;
                bool moveassignable;
                bool constant;
                Wide::Util::optional<CodeGen> codegen;
                CodeGen& GetCodegen(AggregateType* self, Codegen::Generator& g) {
                    if (!codegen) codegen = CodeGen(self, *this, g);
                    return *codegen;
                }
            };
            std::unique_ptr<OverloadResolvable> CopyAssignmentOperator;
            std::unique_ptr<OverloadResolvable> MoveAssignmentOperator;
            std::unique_ptr<OverloadResolvable> CopyConstructor;
            std::unique_ptr<OverloadResolvable> MoveConstructor;
            std::unique_ptr<OverloadResolvable> DefaultConstructor;

            Wide::Util::optional<Layout> layout;
            Layout& GetLayout() {
                if (!layout) layout = Layout(GetContents(), analyzer);
                return *layout;
            }
        public:
            AggregateType(Analyzer& a) : Type(a) {}

            unsigned GetFieldIndex(unsigned num) { return GetLayout().FieldIndices[num]; }
            unsigned GetOffset(unsigned num) { return GetLayout().Offsets[num]; }

            std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num);
            
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetConstantContext() override;
            llvm::Type* GetLLVMType(Codegen::Generator& g) override final;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet();
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) override;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override; 
            std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c) override;

            bool IsCopyConstructible(Lexer::Access access) override;
            bool IsMoveConstructible(Lexer::Access access) override;
            bool IsCopyAssignable(Lexer::Access access) override;
            bool IsMoveAssignable(Lexer::Access access) override;
            bool IsComplexType(Codegen::Generator& g) override;
        };
    }
}