#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {

            virtual const std::vector<Type*>& GetContents() = 0;

            struct Layout {
                struct CodeGen {
                    CodeGen(AggregateType* self, Layout& lay, llvm::Module* module);
                    bool IsComplex;
                    llvm::Type* llvmtype;
                    llvm::Type* GetLLVMType(AggregateType* agg, llvm::Module* module);
                };
                Layout(const std::vector<Type*>& types, Analyzer& a);

                std::size_t allocsize;
                std::size_t align;

                std::vector<unsigned> Offsets;
                std::vector<unsigned> FieldIndices;
                std::vector<std::function<llvm::Type*(llvm::Module* module)>> llvmtypes;

                bool copyconstructible;
                bool copyassignable;
                bool moveconstructible;
                bool moveassignable;
                bool constant;
                Wide::Util::optional<CodeGen> codegen;
                CodeGen& GetCodegen(AggregateType* self, llvm::Module* module) {
                    if (!codegen) codegen = CodeGen(self, *this, module);
                    return *codegen;
                }
            };
            OverloadSet* Constructors = nullptr;
            OverloadSet* NonDefaultConstructors = nullptr;
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

            llvm::Constant* GetRTTI(llvm::Module* module) override;
            unsigned GetFieldIndex(unsigned num) { return GetLayout().FieldIndices[num]; }
            unsigned GetOffset(unsigned num) { return GetLayout().Offsets[num]; }

            std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num);
            
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetConstantContext() override;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet();
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) override;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override; 
            std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c) override;

            bool IsCopyConstructible(Lexer::Access access) override;
            bool IsMoveConstructible(Lexer::Access access) override;
            bool IsCopyAssignable(Lexer::Access access) override;
            bool IsMoveAssignable(Lexer::Access access) override;
            bool IsComplexType(llvm::Module* module) override;
            bool IsEliminateType() override final;
            bool HasMemberOfType(Type* t);
        };
    }
}