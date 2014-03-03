#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Hashers.h>
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
    class NamedDecl;
}

namespace Wide {
    namespace Codegen {
        class Generator;
    }
    namespace AST {
        struct Module;
        struct FunctionBase;
        struct Expression;
        struct ModuleLevelDeclaration;
        struct FunctionOverloadSet;
        struct Type;
        struct DeclContext;
        struct Identifier;
        struct TemplateType;
    };
    namespace Semantic {
        class ClangTU;
        struct Callable;
        class Function;
        class Module;
        class ClangNamespace;
        class ClangType;
        struct Type;
        class ConstructorType;
        struct ConcreteExpression;
        class LvalueType;
        class RvalueType;
        class FunctionType;
        class ClangTemplateClass;
        class OverloadSet;       
        class UserDefinedType;
        class IntegralType;
        class PointerType;
        struct OverloadResolvable;
        class FloatType;
        struct NullType;
        struct Result;
        class TupleType;
        class StringType;
        class TemplateType;
        class Analyzer {
            Module* global;
            std::unordered_map<std::unordered_set<OverloadResolvable*>, OverloadSet*, SetTypeHasher> callable_overload_sets;
            std::unordered_map<std::string, ClangTU> headers;
            std::unordered_map<clang::QualType, Type*, ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, ClangNamespace*> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, FunctionType*, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<const AST::FunctionBase*, std::unordered_map<std::vector<Type*>, Function*, VectorTypeHasher>> WideFunctions;
            std::unordered_map<Type*, LvalueType*> LvalueTypes;
            std::unordered_map<Type*, Type*> RvalueTypes;
            std::unordered_map<Type*, ConstructorType*> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, ClangTemplateClass*> ClangTemplateClasses;
            std::unordered_map<const AST::FunctionOverloadSet*, std::unordered_map<Type*, OverloadSet*>> OverloadSets;
            std::unordered_map<unsigned, FloatType*> FloatTypes;
            std::unordered_map<std::unordered_set<clang::NamedDecl*>, std::unordered_map<Type*, OverloadSet*>, SetTypeHasher> clang_overload_sets;
            std::unordered_map<const AST::Type*, std::unordered_map<Type*, UserDefinedType*>> UDTs;
            std::unordered_map<const AST::Module*, Module*> WideModules;
            std::unordered_map<unsigned, std::unordered_map<bool, IntegralType*>> integers;
            std::unordered_map<Type*, PointerType*> Pointers;
            std::unordered_map<OverloadSet*, std::unordered_map<OverloadSet*, OverloadSet*>> CombinedOverloadSets;
            std::unordered_map<const AST::FunctionBase*, OverloadResolvable*> FunctionCallables;
            std::unordered_map<const AST::TemplateType*, OverloadResolvable*> TemplateTypeCallables;
            std::unordered_map<std::vector<Type*>, TupleType*, VectorTypeHasher> tupletypes;
            std::unordered_map<std::string, StringType*> LiteralStringTypes;

            const Options::Clang* clangopts;

            NullType* null;
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
            //Type* GetNothingFunctorType();
            Type* GetTypeForString(std::string str);
            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangTU& from, clang::DeclContext* dc);
            FunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t);
            Module* GetWideModule(const AST::Module* m, Module* higher);
            Function* GetWideFunction(const AST::FunctionBase* p, Type* context, const std::vector<Type*>&, std::string name);
            LvalueType* GetLvalueType(Type* t);
            Type* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangTU& from, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet();
            OverloadSet* GetOverloadSet(std::unordered_set<OverloadResolvable*> c, Type* nonstatic = nullptr);
            OverloadSet* GetOverloadSet(OverloadResolvable* c);
            OverloadSet* GetOverloadSet(const AST::FunctionOverloadSet* set, Type* nonstatic, std::string name);
            OverloadSet* GetOverloadSet(OverloadSet*, OverloadSet*, Type* context = nullptr);
            OverloadSet* GetOverloadSet(std::unordered_set<clang::NamedDecl*> decls, ClangTU* from, Type* context);
            UserDefinedType* GetUDT(const AST::Type*, Type* context, std::string name);
            IntegralType* GetIntegralType(unsigned, bool);
            PointerType* GetPointerType(Type* to);
            FloatType* GetFloatType(unsigned);
            Module* GetGlobalModule();
            TupleType* GetTupleType(std::vector<Type*> types);
            OverloadResolvable* GetCallableForFunction(const AST::FunctionBase* f, Type* context, std::string name);
            OverloadResolvable* GetCallableForTemplateType(const AST::TemplateType* t, Type* context);
            TemplateType* GetTemplateType(const AST::TemplateType* t, Type* context, std::vector<Type*> arguments, std::string name);
            ConcreteExpression AnalyzeExpression(Type* t, const AST::Expression* e, std::function<void(ConcreteExpression)>);
            Wide::Util::optional<ConcreteExpression> LookupIdentifier(Type* context, const AST::Identifier* ident);

            Analyzer(const Options::Clang&, Codegen::Generator&, const AST::Module*);     

            ClangTU* LoadCPPHeader(std::string file, Lexer::Range where);
            
            ~Analyzer();
        };
        bool IsRvalueType(Type* t);
        bool IsLvalueType(Type* t);
        Lexer::Access GetAccessSpecifier(Type* from, Type* to, Analyzer& a);
        Lexer::Access GetAccessSpecifier(Context c, Type* to);
        void AnalyzeExportedFunctions(Analyzer& a);
        std::string GetNameForOperator(Lexer::TokenType t);
    }
}
