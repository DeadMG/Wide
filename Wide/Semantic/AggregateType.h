#pragma once

#include <Wide/Semantic/Type.h>
#include <boost/optional.hpp>

namespace Wide {
    namespace Semantic {
        class AggregateType : public Type {
            virtual std::vector<Type*> GetMembers() = 0;
            virtual std::vector<std::shared_ptr<Expression>> GetDefaultInitializerForMember(unsigned) { return {}; }
            virtual bool HasDeclaredDynamicFunctions() { return false; }
            virtual Wide::Util::optional<std::pair<unsigned, Lexer::Range>> SizeOverride() { return Wide::Util::none; }
            virtual Wide::Util::optional<std::pair<unsigned, Lexer::Range>> AlignOverride() { return Wide::Util::none; }
            virtual std::string GetLLVMTypeName();
            void EmitConstructor(CodegenContext& con, std::vector<std::shared_ptr<Expression>> inits);
            void EmitAssignmentOperator(CodegenContext& con, std::vector<std::shared_ptr<Expression>> exprs);
            OverloadSet* MaybeCreateSet(Wide::Util::optional<std::unique_ptr<OverloadResolvable>>&, std::function<bool(Type*, unsigned)> should, std::function<std::unique_ptr<OverloadResolvable>()> create);

            struct Properties {
                Properties(AggregateType* self);
                bool triviallycopyconstructible = true;
                bool triviallydestructible = true;
                bool copyconstructible;
                bool copyassignable;
                bool moveconstructible;
                bool moveassignable;
                bool constant;
            };
            Wide::Util::optional<Properties> props;
            Properties& GetProperties();

            struct AggregateFunction;
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
                    boost::optional<unsigned> FieldIndex;
                };
                std::vector<Location> Offsets;

                bool hasvptr = false;
                Type* PrimaryBase = nullptr;

                Wide::Util::optional<CodeGen> codegen;
                CodeGen& GetCodegen(AggregateType* self, llvm::Module* module) {
                    if (!codegen) codegen = CodeGen(self, *this, module);
                    return *codegen;
                }
            };
            std::shared_ptr<Expression> GetConstructorSelf(llvm::Function*& func);
            std::shared_ptr<Expression> GetMemberFromThis(std::shared_ptr<Expression> self, std::function<unsigned()> offset, Type* result);
            std::vector<std::shared_ptr<Expression>> GetConstructorInitializers(std::shared_ptr<Expression> self, Context c, std::function<std::vector<std::shared_ptr<Expression>>(unsigned)> initializers);
            std::vector<std::shared_ptr<Expression>> GetAssignmentOpExpressions(std::shared_ptr<Expression> self, Context c, std::function<std::shared_ptr<Expression>(unsigned)> initializers);

            std::string CopyAssignmentName;
            llvm::Function* CopyAssignmentFunction = nullptr;
            Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> CopyAssignmentExpressions;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> CopyAssignmentOperator;
            OverloadSet* GetCopyAssignmentOperator();
            void CreateCopyAssignmentExpressions();
            void EmitCopyAssignmentOperator(llvm::Module*);

            std::string MoveAssignmentName;
            llvm::Function* MoveAssignmentFunction = nullptr;
            Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> MoveAssignmentExpressions;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> MoveAssignmentOperator;
            OverloadSet* GetMoveAssignmentOperator();
            void CreateMoveAssignmentExpressions();
            void EmitMoveAssignmentOperator(llvm::Module*);

            std::string CopyConstructorName;
            llvm::Function* CopyConstructorFunction = nullptr;
            Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> CopyConstructorInitializers;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> CopyConstructor;
            OverloadSet* GetCopyConstructor();
            void CreateCopyConstructorInitializers();
            void EmitCopyConstructor(llvm::Module*);

            std::string MoveConstructorName;
            llvm::Function* MoveConstructorFunction = nullptr;
            Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> MoveConstructorInitializers;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> MoveConstructor;
            OverloadSet* GetMoveConstructor();
            void CreateMoveConstructorInitializers();
            void EmitMoveConstructor(llvm::Module*);

            std::string DefaultConstructorName;
            llvm::Function* DefaultConstructorFunction = nullptr;
            Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> DefaultConstructorInitializers;
            Wide::Util::optional<std::unique_ptr<OverloadResolvable>> DefaultConstructor;
            OverloadSet* GetDefaultConstructor();
            void CreateDefaultConstructorInitializers();
            void EmitDefaultConstructor(llvm::Module*);

            std::string DestructorName;
            std::vector<std::function<void(CodegenContext&)>> destructors;
            Wide::Util::optional<Layout> layout;
            Layout& GetLayout() {
                if (!layout) layout = Layout(this, analyzer);
                return *layout;
            }
        protected:
            std::function<llvm::Function*(llvm::Module*)> CreateDestructorFunction() override;
            struct AggregateConstructors {
                bool default_constructor;
                bool copy_constructor;
                bool move_constructor;
            };
            struct AggregateAssignmentOperators {
                bool move_operator;
                bool copy_operator;
            };
            void PrepareExportedFunctions(AggregateAssignmentOperators, AggregateConstructors, bool);
            OverloadSet* CreateAssignmentOperatorOverloadSet(AggregateAssignmentOperators which);
            OverloadSet* CreateConstructorOverloadSet(AggregateConstructors which);
            void Export(llvm::Module* mod) override;
        public:
            enum class SpecialFunction {
                DefaultConstructor,
                CopyConstructor,
                MoveConstructor,
                MoveAssignmentOperator,
                CopyAssignmentOperator,
                Destructor
            };
            std::string GetSpecialFunctionName(SpecialFunction f);
            AggregateType(Analyzer& a);

            std::function<llvm::Constant*(llvm::Module*)> GetRTTI() override;
            unsigned GetOffset(unsigned num) { return GetLayout().Offsets[num].ByteOffset; }

            MemberLocation GetLocation(unsigned i);

            virtual std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num);
            
            std::size_t size() override;
            std::size_t alignment() override;
            llvm::Type* GetLLVMType(llvm::Module* module) override;
            Type* GetConstantContext() override;

            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName type, Parse::Access access, OperatorAccess) override;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override; 
            std::function<void(CodegenContext&)> BuildDestruction(Expression::InstanceKey, std::shared_ptr<Expression> self, Context c, bool devirtualize) override;
            std::shared_ptr<Expression> AccessVirtualPointer(Expression::InstanceKey key, std::shared_ptr<Expression> self) override final;

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