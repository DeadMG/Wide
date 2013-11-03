#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/ClangOptions.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _MSC_VER
#include <Wide/Semantic/ClangTU.h>
#include <clang/AST/Type.h>
#endif

namespace llvm {
    class Type;
    class LLVMContext;
    class Value;
    class Module;
}

namespace clang {
    template<unsigned> class UnresolvedSet;
    class QualType;
    class DeclContext;
    class ClassTemplateDecl;
}

namespace Wide {
    class Library;
    namespace ClangUtil {
        class ClangTU;
    }
    namespace Codegen {
        class Generator;
    }
    namespace AST {
        struct Module;
        struct Function;
        struct Expression;
        struct ModuleLevelDeclaration;
        struct FunctionOverloadSet;
        struct Type;
        struct DeclContext;
        struct Identifier;
    };
    namespace Semantic {
        struct Callable;
        struct Expression;
        class Function;
        class Module;
        class ClangNamespace;
        class ClangType;
        struct Type;
        class ConstructorType;
        class LvalueType;
        class RvalueType;
        class FunctionType;
        class ClangOverloadSet;
        class ClangTemplateClass;
        class OverloadSet;       
        class UserDefinedType;
        class IntegralType;
        class PointerType;
        class FloatType;
        struct NullType;
        struct Result;
        struct VectorTypeHasher {
            std::size_t operator()(const std::vector<Type*>& t) const;
        };
        struct SetTypeHasher {
            std::size_t operator()(const std::unordered_set<Callable*>& t) const {
                std::size_t hash = 0;
                for(auto ty : t)
                    hash += std::hash<Callable*>()(ty);
                return hash;
            }
        };
        class Analyzer {
            Module* global;
            std::unordered_map<std::unordered_set<Callable*>, OverloadSet*, SetTypeHasher> callable_overload_sets;
            std::unordered_map<std::string, ClangUtil::ClangTU> headers;
            std::unordered_map<clang::QualType, Type*, ClangUtil::ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, ClangNamespace*> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, FunctionType*, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<const AST::Function*, std::unordered_map<std::vector<Type*>, Function*, VectorTypeHasher>> WideFunctions;
            std::unordered_map<Type*, LvalueType*> LvalueTypes;
            std::unordered_map<Type*, Type*> RvalueTypes;
            std::unordered_map<Type*, ConstructorType*> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, ClangTemplateClass*> ClangTemplateClasses;
            std::unordered_map<const AST::FunctionOverloadSet*, std::unordered_map<Type*, OverloadSet*>> OverloadSets;
            std::unordered_map<unsigned, FloatType*> FloatTypes;

            std::unordered_map<const AST::Type*, std::unordered_map<Type*, UserDefinedType*>> UDTs;
            std::unordered_map<const AST::Module*, Module*> WideModules;
            std::unordered_map<unsigned, std::unordered_map<bool, IntegralType*>> integers;
            std::unordered_map<Type*, PointerType*> Pointers;
            std::unordered_map<OverloadSet*, std::unordered_map<OverloadSet*, OverloadSet*>> CombinedOverloadSets;

            const Options::Clang* clangopts;

            NullType* null;
            Type* LiteralStringType;
            Type* Void;
            Type* Boolean;
            Type* NothingFunctor;
            OverloadSet* EmptyOverloadSet;

        public:
            void AddClangType(clang::QualType t, Type* match);

            Wide::Memory::Arena arena;

            Codegen::Generator* gen;

            Type* GetVoidType();
            Type* GetNullType();
            Type* GetBooleanType();
            Type* GetNothingFunctorType();
            Type* GetLiteralStringType();
            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangUtil::ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangUtil::ClangTU& from, clang::DeclContext* dc);
            FunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t);
            Module* GetWideModule(const AST::Module* m, Module* higher);
            Function* GetWideFunction(const AST::Function* p, Type* context, const std::vector<Type*>& = std::vector<Type*>());
            LvalueType* GetLvalueType(Type* t);
            Type* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangUtil::ClangTU& from, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet();
            OverloadSet* GetOverloadSet(Callable* c);
            OverloadSet* GetOverloadSet(const AST::FunctionOverloadSet* set, Type* nonstatic);
            OverloadSet* GetOverloadSet(OverloadSet*, OverloadSet*);
            UserDefinedType* GetUDT(const AST::Type*, Type* context);
            IntegralType* GetIntegralType(unsigned, bool);
            PointerType* GetPointerType(Type* to);
            FloatType* GetFloatType(unsigned);
            Module* GetGlobalModule();
            
            Expression AnalyzeExpression(Type* t, const AST::Expression* e);
            Wide::Util::optional<Expression> LookupIdentifier(Type* context, const AST::Identifier* ident);

            Analyzer(const Options::Clang&, Codegen::Generator&, const AST::Module*);     

            ClangUtil::ClangTU* LoadCPPHeader(std::string file, Lexer::Range where);
            
            ~Analyzer();
        };
        bool IsRvalueType(Type* t);
        bool IsLvalueType(Type* t);
    }
}
