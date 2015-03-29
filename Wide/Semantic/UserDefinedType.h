#pragma once

#include <Wide/Semantic/AggregateType.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <tuple>

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
        class FunctionSkeleton;
        class UserDefinedType : public AggregateType, public TupleInitializable, public ConstructorContext {
            std::unique_ptr<Semantic::Error> AlignOverrideError;
            std::unique_ptr<Semantic::Error> SizeOverrideError;
            std::unordered_map<Parse::Name, std::unordered_map<Type*, std::unique_ptr<Semantic::Error>>> FunctionImportErrors;

            struct ImportConstructorCallable;
            struct ImportConstructorResolvable;
            FunctionSkeleton* GetWideFunction(const Parse::FunctionBase* base, Parse::Name name);
            std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> GetNonstaticLookup(const Parse::FunctionBase* base, Parse::Name name);
            std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> GetNonstaticLookup(Expression::InstanceKey key, std::shared_ptr<Expression>);

            std::vector<Type*> GetMembers() { return GetMemberData().members; }
            std::vector<std::shared_ptr<Expression>> GetDefaultInitializerForMember(unsigned);

            const Parse::Type* type;
            std::string source_name;
            Type* context;
            std::unordered_map<Type*, std::unique_ptr<OverloadResolvable>> AssignmentOperatorImportResolvables;

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
                VTableData(const VTableData& other) = default;
                VTableData& operator=(const VTableData& other) = default;
                VTableLayout funcs;
                // Relative to the first vptr, not the front of the vtable.
                std::unordered_map<const Parse::DynamicFunction*, unsigned> VTableIndices;
                bool is_abstract;
                bool dynamic_destructor;
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
                std::unordered_map<Parse::Name, std::unordered_map<Type*, Lexer::Range>> BaseImports;
                std::unordered_map<Type*, std::unique_ptr<OverloadResolvable>> imported_constructors;
                std::vector<std::unique_ptr<Semantic::Error>> ImportErrors;
            };
            Wide::Util::optional<MemberData> Members;
            MemberData& GetMemberData() {
                if (!Members) Members = MemberData(this);
                return *Members;
            }

            struct ExportData {
                ExportData(UserDefinedType* self) : exported(false) {}
                bool exported;
                // First is reference, second is value.
                std::unordered_map<std::string, std::pair<std::string, std::string>> MemberPropertyNames;
            };
            Wide::Util::optional<ExportData> Exports;
            ExportData& GetExportData() {
                if (!Exports) Exports = ExportData(this);
                return *Exports;
            }

            struct DefaultData {
                DefaultData(UserDefinedType* self);
                DefaultData(DefaultData&&);
                DefaultData& operator=(DefaultData&&);

                OverloadSet* SimpleConstructors;
                OverloadSet* SimpleAssOps;
                AggregateAssignmentOperators AggregateOps;
                AggregateConstructors AggregateCons;
                bool IsComplex;
            };
            Wide::Util::optional<DefaultData> Defaults;
            DefaultData& GetDefaultData() {
                if (!Defaults) Defaults = DefaultData(this);
                return *Defaults;
            }

            std::unique_ptr<OverloadResolvable> DefaultConstructor;
            bool HasDeclaredDynamicFunctions() override final;

            Type* GetSelfAsType() override final { return this; }
            std::unordered_map<ClangTU*, clang::CXXRecordDecl*> clangtypes;
            // Virtual function support functions.
            std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry) override final;
            VTableLayout ComputePrimaryVTableLayout() override final;
            Type* GetVirtualPointerType() override final;
            std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets() override final;
            std::vector<member> GetConstructionMembers() override final;
            Type* GetPrimaryBase() override final { return GetBaseData().PrimaryBase; }
            Wide::Util::optional<std::pair<unsigned, Lexer::Range>> SizeOverride() override final;
            Wide::Util::optional<std::pair<unsigned, Lexer::Range>> AlignOverride() override final;
            std::function<llvm::Function*(llvm::Module*)> CreateDestructorFunction() override final;
            bool HasVirtualDestructor() override final;
            bool IsNonstaticMemberContext() override final { return true; }
            std::string GetLLVMTypeName() override final;
        public:
            std::function<llvm::Constant*(llvm::Module*)> GetRTTI() override final;
            UserDefinedType(const Parse::Type* t, Analyzer& a, Type* context, std::string);
            Type* GetContext() override final { return context; }
            bool HasMember(Parse::Name name);
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context) override final;
            using Type::AccessMember;
            std::function<void(CodegenContext&)> BuildDestruction(Expression::InstanceKey key, std::shared_ptr<Expression> self, Context c, bool devirtualize) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName member, Parse::Access access, OperatorAccess) override final;
            Type* GetConstantContext() override final;

            bool IsCopyConstructible(Parse::Access access) override final;
            bool IsMoveConstructible(Parse::Access access) override final;
            bool IsCopyAssignable(Parse::Access access) override final;
            bool IsMoveAssignable(Parse::Access access) override final;
            bool IsTriviallyCopyConstructible() override final;
            bool IsTriviallyDestructible() override final;
            bool IsSourceATarget(Type* first, Type* second, Type* context) override final;
            std::vector<Type*> GetBases() override final;
            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::shared_ptr<Expression> PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) override final;
            std::shared_ptr<Expression> AccessStaticMember(std::string name, Context c) override final;
            std::string explain() override final;
            Wide::Util::optional<unsigned> GetVirtualFunctionIndex(const Parse::DynamicFunction* func);
            bool IsFinal() override final;
            std::string GetExportBody() override final;
            void Export(llvm::Module* mod) override final;
            bool AlwaysKeepInMemory(llvm::Module* mod) override final;
        };
    }
}