#pragma once

#include <Wide/Semantic/AggregateType.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)

namespace Wide {
    namespace Parse {
        struct Type;
        struct Expression;
        struct DynamicFunction;
    }
    namespace Semantic {
        class Function;
        class OverloadSet;
        class Module;
        class UserDefinedType : public AggregateType, public TupleInitializable, public MemberFunctionContext, public ConstructorContext {

            std::vector<Type*> GetMembers() { return GetMemberData().members; }

            const Parse::Type* type;
            std::string source_name;
            Type* context;

            Wide::Util::optional<bool> UDCCache;
            bool UserDefinedComplex();
                        
            struct BaseData {
                BaseData(UserDefinedType* self);
                BaseData(BaseData&& other);
                BaseData& operator=(BaseData&& other);
                std::vector<Type*> bases;
                Type* PrimaryBase = nullptr;
            };
            Wide::Util::optional<BaseData> Bases;
            BaseData& GetBaseData() {
                if (!Bases) Bases = BaseData(this);
                return *Bases;
            }

            struct VTableData {
                VTableData(UserDefinedType* self);
                VTableData(VTableData&& other);
                VTableData& operator=(VTableData&& other);
                VTableLayout funcs;
                // Relative to the first vptr, not the front of the vtable.
                std::unordered_map<const Parse::DynamicFunction*, unsigned> VTableIndices;
            };
            Wide::Util::optional<VTableData> Vtable;
            VTableData& GetVtableData() {
                if (!Vtable) Vtable = VTableData(this);
                return *Vtable;
            }

            struct MemberData {
                MemberData(UserDefinedType* self);
                MemberData(MemberData&& other);
                MemberData& operator=(MemberData&& other);
                // Actually a list of member variables
                std::unordered_map<std::string, unsigned> member_indices;
                std::vector<const Parse::Expression*> NSDMIs;
                bool HasNSDMI = false;
                std::vector<Type*> members;
            };
            Wide::Util::optional<MemberData> Members;
            MemberData& GetMemberData() {
                if (!Members) Members = MemberData(this);
                return *Members;
            }

            std::unique_ptr<OverloadResolvable> DefaultConstructor;
            bool HasDeclaredDynamicFunctions() override final;

            Type* GetSelfAsType() override final { return this; }
            std::unordered_map<ClangTU*, clang::QualType> clangtypes;
            // Virtual function support functions.
            std::shared_ptr<Expression> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) override final;
            VTableLayout ComputePrimaryVTableLayout() override final;
            Type* GetVirtualPointerType() override final;
            std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets() override final;
            std::vector<member> GetConstructionMembers() override final;
            Type* GetPrimaryBase() override final { return GetBaseData().PrimaryBase; }
            Wide::Util::optional<unsigned> SizeOverride() override final;
            Wide::Util::optional<unsigned> AlignOverride() override final; 
            llvm::Function* CreateDestructorFunction(llvm::Module* module) override final;
        public:
            llvm::Constant* GetRTTI(llvm::Module* module) override final;
            UserDefinedType(const Parse::Type* t, Analyzer& a, Type* context, std::string);
            Type* GetContext() override final { return context; }
            bool HasMember(std::string name);            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> t, std::string name, Context) override final;
            using Type::AccessMember;
            std::function<void(CodegenContext&)> BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Lexer::TokenType member, Lexer::Access access) override final;
            Type* GetConstantContext() override final;

            bool IsCopyConstructible(Lexer::Access access) override final;
            bool IsMoveConstructible(Lexer::Access access) override final;
            bool IsCopyAssignable(Lexer::Access access) override final;
            bool IsMoveAssignable(Lexer::Access access) override final;
            bool IsTriviallyCopyConstructible() override final;
            bool IsTriviallyDestructible() override final;
            bool IsSourceATarget(Type* first, Type* second, Type* context) override final;
            std::vector<Type*> GetBases() override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) override final;
            std::string explain() override final;
            Wide::Util::optional<unsigned> GetVirtualFunctionIndex(const Parse::DynamicFunction* func);
        };
    }
}