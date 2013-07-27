#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <Wide/Util/MemoryArena.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/ClangOptions.h>

#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)

#include <clang/Basic/FileManager.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Basic/TargetInfo.h>
#include <llvm/IR/DataLayout.h>

#pragma warning(pop)

#ifndef _MSC_VER
#include <Wide/Semantic/ClangTU.h>
#include <clang/AST/Type.h>
#include <Wide/Semantic/Type.h>
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
    };
    namespace Semantic {
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
        struct NullType;
        enum ConversionRank;
        struct Result;
        struct VectorTypeHasher {
            std::size_t operator()(const std::vector<Type*>& t) const;
        };
        class Analyzer {
            std::unordered_map<std::string, ClangUtil::ClangTU> headers;
            std::unordered_map<clang::QualType, Type*, ClangUtil::ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, ClangNamespace*> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, FunctionType*, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<AST::Function*, std::unordered_map<std::vector<Type*>, Function*, VectorTypeHasher>> WideFunctions;
            std::unordered_map<Type*, LvalueType*> LvalueTypes;
            std::unordered_map<Type*, Type*> RvalueTypes;
            std::unordered_map<Type*, ConstructorType*> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, ClangTemplateClass*> ClangTemplateClasses;
            std::unordered_map<AST::FunctionOverloadSet*, std::unordered_map<Type*, OverloadSet*>> OverloadSets;

            std::unordered_map<AST::DeclContext*, Type*> DeclContexts;
            std::unordered_map<AST::Type*, std::unordered_map<Type*, UserDefinedType*>> UDTs;
            std::unordered_map<AST::Module*, Module*> WideModules;
            std::unordered_map<unsigned, std::unordered_map<bool, IntegralType*>> integers;
            std::unordered_map<Type*, PointerType*> Pointers;

            const Options::Clang* clangopts;

            NullType* null;
            Type* LiteralStringType;
            Type* Void;
            Type* Boolean;
            Type* NothingFunctor;

        public:
            ConversionRank RankConversion(Type* from, Type* to);

            void AddClangType(clang::QualType t, Type* match);

            Wide::Memory::Arena arena;

            Codegen::Generator* gen;

            Type* GetVoidType();
            Type* GetNullType();
            Type* GetBooleanType();
            Type* GetNothingFunctorType();
            Type* GetLiteralStringType();
            
            void AddDefaultConstructor(AST::Type* t, UserDefinedType* ty);
            void AddCopyConstructor(AST::Type* t, UserDefinedType* ty);
            void AddMoveConstructor(AST::Type* t, UserDefinedType* ty);
            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangUtil::ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangUtil::ClangTU& from, clang::DeclContext* dc);
            FunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t);
            Module* GetWideModule(AST::Module* m);
            Function* GetWideFunction(AST::Function* p, UserDefinedType* nonstatic = nullptr, const std::vector<Type*>& = std::vector<Type*>());
            LvalueType* GetLvalueType(Type* t);
            Type* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangUtil::ClangTU& from, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet(AST::FunctionOverloadSet* set, Type* nonstatic = nullptr);
            UserDefinedType* GetUDT(AST::Type*, Type* context);
            Type* GetDeclContext(AST::DeclContext* con);
            IntegralType* GetIntegralType(unsigned, bool);
            PointerType* GetPointerType(Type* to);
            
            Expression AnalyzeExpression(Type* t, AST::Expression* e);
            Expression LookupIdentifier(AST::ModuleLevelDeclaration* decl, std::string ident);

            Analyzer(const Options::Clang&, Codegen::Generator*);     

            ClangUtil::ClangTU* LoadCPPHeader(std::string file);  

            void operator()(AST::Module*);
            ~Analyzer();
        };
    }
}
