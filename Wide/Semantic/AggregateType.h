#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {
            virtual std::vector<Type*> GetMembers() = 0;
            virtual bool HasDeclaredDynamicFunctions() { return false; }
            virtual Wide::Util::optional<unsigned> SizeOverride() { return Wide::Util::none; }
            virtual Wide::Util::optional<unsigned> AlignOverride() { return Wide::Util::none; }

            struct Layout {
                struct CodeGen {
                    CodeGen(AggregateType* self, Layout& lay, llvm::Module* module);
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

                bool triviallycopyconstructible = true;
                bool triviallydestructible = true;
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
            std::vector<std::function<void(CodegenContext&)>> destructors;
            Wide::Util::optional<Layout> layout;
            Layout& GetLayout() {
                if (!layout) layout = Layout(this, analyzer);
                return *layout;
            }
        protected:
            llvm::Function* CreateDestructorFunction(llvm::Module* module) override;
        public:
            AggregateType(Analyzer& a);

            llvm::Constant* GetRTTI(llvm::Module* module) override;
            unsigned GetOffset(unsigned num) { return GetLayout().Offsets[num].ByteOffset; }

            MemberLocation GetLocation(unsigned i);

            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num);
            
            std::size_t size() override;
            std::size_t alignment() override;
            llvm::Type* GetLLVMType(llvm::Module* module) override;
            Type* GetConstantContext() override;
            
            OverloadSet* CreateNondefaultConstructorOverloadSet();
            OverloadSet* CreateOperatorOverloadSet(Lexer::TokenType type, Parse::Access access) override;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override; 
            std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) override;
            std::shared_ptr<Expression> GetVirtualPointer(std::shared_ptr<Expression> self) override final;

            bool IsCopyConstructible(Parse::Access access) override;
            bool IsMoveConstructible(Parse::Access access) override;
            bool IsCopyAssignable(Parse::Access access) override;
            bool IsMoveAssignable(Parse::Access access) override;
            bool IsEmpty() override final;
            bool IsTriviallyDestructible() override;
            bool IsTriviallyCopyConstructible() override;
            bool HasMemberOfType(Type* t);

            std::unordered_map<unsigned, std::unordered_set<Type*>> GetEmptyLayout() override final { return GetLayout().EmptyTypes; }
        };
    }
}