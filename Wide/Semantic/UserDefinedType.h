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
    namespace AST {
        struct Type;
        struct Expression;
        struct Function;
    }
    namespace Semantic {
        class Function;
        class OverloadSet;
        class Module;
        class UserDefinedType : public AggregateType, public TupleInitializable, public MemberFunctionContext, public ConstructorContext {

            const std::vector<Type*>& GetContents() { return GetMemberData().contents; }

            const AST::Type* type;
            std::string source_name;
            Type* context;

            Wide::Util::optional<bool> UDCCache;
            bool UserDefinedComplex();

            Wide::Util::optional<bool> BCCache;
            bool BinaryComplex(llvm::Module* module);
            
            struct MemberData {
                MemberData(UserDefinedType* self);
                MemberData(MemberData&& other);
                MemberData& operator=(MemberData&& other);
                // Actually a list of member variables
                std::unordered_map<std::string, unsigned> members;
                std::vector<const AST::Expression*> NSDMIs;
                bool HasNSDMI = false;
                VTableLayout funcs;
                std::unordered_map<const AST::Function*, unsigned> VTableIndices;
                std::vector<Type*> contents;
                std::vector<Type*> bases;
            };
            Wide::Util::optional<MemberData> Members;
            MemberData& GetMemberData() {
                if (!Members) Members = MemberData(this);
                return *Members;
            }

            std::unique_ptr<OverloadResolvable> DefaultConstructor;

            Type* GetSelfAsType() override final { return this; }
            std::unordered_map<ClangTU*, clang::QualType> clangtypes;
            // Virtual function support functions.
            std::unique_ptr<Expression> FunctionPointerFor(VTableLayout::VirtualFunction entry, unsigned offset);
            std::unique_ptr<Expression> VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) override final;
            VTableLayout ComputeVTableLayout() override final;
            Type* GetVirtualPointerType() override final;
            std::vector<std::pair<Type*, unsigned>> GetBasesAndOffsets() override final;
            std::vector<Type*> GetBases() override final;
            bool IsDynamic();
            std::vector<member> GetMembers() override final;
        public:
            llvm::Constant* GetRTTI(llvm::Module* module) override final;
            UserDefinedType(const AST::Type* t, Analyzer& a, Type* context, std::string);           
            Type* GetContext() override final { return context; }
            bool HasMember(std::string name);            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context) override final;
            std::unique_ptr<Expression> BuildDestructorCall(std::unique_ptr<Expression> self, Context c) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType member, Lexer::Access access) override final;

            bool IsCopyConstructible(Lexer::Access access) override final;
            bool IsMoveConstructible(Lexer::Access access) override final;
            bool IsCopyAssignable(Lexer::Access access) override final;
            bool IsMoveAssignable(Lexer::Access access) override final;
            bool IsComplexType(llvm::Module* module) override final;
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;

            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple() override final;
            std::unique_ptr<Expression> PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) override final;
            InheritanceRelationship IsDerivedFrom(Type* other) override final;
            std::unique_ptr<Expression> AccessBase(std::unique_ptr<Expression> self, Type* other) override final;
            std::string explain() override final;
            Wide::Util::optional<unsigned> GetVirtualFunctionIndex(const AST::Function* func);
            std::unique_ptr<Expression> GetVirtualPointer(std::unique_ptr<Expression> self) override final;
        };
    }
}