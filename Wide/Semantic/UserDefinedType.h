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

            const std::vector<Type*>& GetContents() { return contents; }

            const AST::Type* type;
            std::string source_name;

            Wide::Util::optional<bool> UDCCache;
            bool UserDefinedComplex(Analyzer& a);

            Wide::Util::optional<bool> BCCache;
            bool BinaryComplex(Analyzer& a);

            std::vector<Type*> contents;
            std::vector<BaseType*> bases;

            // Actually a list of member variables
            std::unordered_map<std::string, unsigned> members;
            std::unordered_map<ClangTU*, clang::QualType> clangtypes;
            std::vector<const AST::Expression*> NSDMIs;
            Type* context;
            bool HasNSDMI = false;
            // User Defined Complex
            std::unordered_map<const AST::Function*, unsigned> VTableIndices;
            // Binary Complex
            Type* GetSelfAsType() override final { return this; }

            // Virtual function support functions.
            std::vector<VirtualFunction> funcs;
            Codegen::Expression* FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset, Analyzer& a) override final;
            std::vector<VirtualFunction> ComputeVTableLayout(Analyzer& a) override final;
            Codegen::Expression* GetVirtualPointer(Codegen::Expression* self, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetVirtualPointerType(Analyzer& a) override final;
            std::vector<std::pair<BaseType*, unsigned>> GetBases(Analyzer& a) override final;
            bool IsDynamic(Analyzer& a);
        public:
            struct member {
                std::string name;
                Type* t;
                unsigned num;
                const AST::Expression* InClassInitializer;
                bool vptr;
            };
            std::vector<member> GetMembers();
            UserDefinedType(const AST::Type* t, Analyzer& a, Type* context, std::string);           
            Type* GetContext(Analyzer& a) override final { return context; }
            bool HasMember(std::string name);            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU, Analyzer& a) override final;
            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression, std::string name, Context c) override final;

            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&, Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType member, Lexer::Access access, Analyzer& a) override final;
            OverloadSet* CreateDestructorOverloadSet(Analyzer& a) override final;

            bool IsCopyConstructible(Analyzer& a, Lexer::Access access) override final;
            bool IsMoveConstructible(Analyzer& a, Lexer::Access access) override final;
            bool IsCopyAssignable(Analyzer& a, Lexer::Access access) override final;
            bool IsMoveAssignable(Analyzer& a, Lexer::Access access) override final;
            bool IsComplexType(Analyzer& a) override final;

            Wide::Util::optional<std::vector<Type*>> GetTypesForTuple(Analyzer& a) override final;
            ConcreteExpression PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) override final;
            InheritanceRelationship IsDerivedFrom(Type* other, Analyzer& a) override final;
            Codegen::Expression* AccessBase(Type* other, Codegen::Expression*, Analyzer& a) override final;
            std::string explain(Analyzer& a) override final;
            Callable* GetCallableForDynamicCall(const AST::Function* func, std::vector<Type*> types, std::string name, Analyzer& a);
        };
    }
}