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
        class UserDefinedType : public AggregateType, public TupleInitializable, public BaseType, public MemberFunctionContext {

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
                std::vector<VirtualFunction> funcs;
                std::unordered_map<const AST::Function*, unsigned> VTableIndices;
                std::vector<Type*> contents;
                std::vector<BaseType*> bases;
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
            std::unique_ptr<Expression> FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset) override final;
            std::vector<VirtualFunction> ComputeVTableLayout() override final;
            Type* GetVirtualPointerType() override final;
            std::vector<std::pair<BaseType*, unsigned>> GetBasesAndOffsets() override final;
            std::vector<BaseType*> GetBases() override final;
            bool IsDynamic();
        public:
            struct member {
                member(Lexer::Range where)
                : location(where) {}
                member(const member&) = delete;
                member(member&& other)
                    : name(std::move(other.name))
                    , t(other.t)
                    , num(other.num)
                    , InClassInitializer(std::move(other.InClassInitializer))
                    , location(std::move(other.location))
                    , vptr(other.vptr) {}
                std::string name;
                Type* t;
                unsigned num;
                std::function<std::unique_ptr<Expression>(std::unique_ptr<Expression>)> InClassInitializer;
                Lexer::Range location;
                bool vptr;
            };
            std::vector<member> GetMembers();
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