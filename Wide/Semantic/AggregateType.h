#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {
            virtual const std::vector<Type*>& GetMembers() = 0;
            virtual bool HasDeclaredDynamicFunctions() = 0;

            struct Layout {
                struct CodeGen {
                    CodeGen(AggregateType* self, Layout& lay, llvm::Module* module);
                    bool IsComplex;
                    llvm::Type* llvmtype;
                    llvm::Type* GetLLVMType(AggregateType* agg, llvm::Module* module);
                };
                Layout(AggregateType* agg, Analyzer& a);

                std::size_t allocsize;
                std::size_t align;

                std::unordered_map<unsigned, std::unordered_set<Type*>> EmptyTypes;
                // Should use vector<Type*>::iterator but not enough guarantees.
                struct Location {
                    Type* ty;
                    unsigned ByteOffset;
                    Wide::Util::optional<unsigned> FieldIndex;
                };
                std::vector<Location> Offsets;

                bool copyconstructible;
                bool copyassignable;
                bool moveconstructible;
                bool moveassignable;
                bool constant;
                bool hasvptr = false;
                Type* PrimaryBase = nullptr;
                unsigned PrimaryBaseNum;

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
                if (!layout) layout = Layout(this, analyzer);
                return *layout;
            }
        public:
            AggregateType(Analyzer& a) : Type(a) {}

            llvm::Constant* GetRTTI(llvm::Module* module) override;
            unsigned GetOffset(unsigned num) { return GetLayout().Offsets[num].ByteOffset; }

            MemberLocation GetLocation(unsigned i);

            std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num);
            
            std::size_t size() override final;
            std::size_t alignment() override final;
            Type* GetConstantContext() override;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet();
            OverloadSet* CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access) override;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override; 
            std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c) override;
            std::unique_ptr<Expression> GetVirtualPointer(std::unique_ptr<Expression> self) override final;

            bool IsCopyConstructible(Lexer::Access access) override;
            bool IsMoveConstructible(Lexer::Access access) override;
            bool IsCopyAssignable(Lexer::Access access) override;
            bool IsMoveAssignable(Lexer::Access access) override;
            bool IsComplexType(llvm::Module* module) override;
            bool IsEmpty() override final;
            bool HasMemberOfType(Type* t);

            std::unordered_map<unsigned, std::unordered_set<Type*>> GetEmptyLayout() override final { return GetLayout().EmptyTypes; }
        };
    }
}