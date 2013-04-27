#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include "../../Util\MemoryArena.h"
#include "Util.h"
#include "../ClangOptions.h"
#include "../LLVMOptions.h"

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
#include "ClangTU.h"
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
       
        struct Result;
        
        struct ClangCommonState {

            ClangCommonState(const Options::Clang& opts);
            const Options::Clang* Options;
            clang::FileManager FileManager;
            std::string errors;
            llvm::raw_string_ostream error_stream;
            clang::DiagnosticsEngine engine;
            std::unique_ptr<clang::TargetInfo> targetinfo;
            clang::CompilerInstance ci;
            clang::HeaderSearch hs;
            llvm::DataLayout layout;
        };
        struct VectorTypeHasher {
            std::size_t operator()(const std::vector<Type*>& t) const;
        };
        class Analyzer {
            std::unordered_map<std::string, ClangUtil::ClangTU> headers;
            std::unordered_map<clang::QualType, Type*, ClangUtil::ClangTypeHasher> ClangTypes;
            std::unordered_map<clang::DeclContext*, ClangNamespace*> ClangNamespaces;
            std::unordered_map<Type*, std::unordered_map<std::vector<Type*>, FunctionType*, VectorTypeHasher>> FunctionTypes;
            std::unordered_map<AST::Module*, Module*> WideModules;
            std::unordered_map<AST::Function*, Function*> WideFunctions;
            std::unordered_map<Type*, LvalueType*> LvalueTypes;
            std::unordered_map<Type*, RvalueType*> RvalueTypes;
            std::unordered_map<Type*, ConstructorType*> ConstructorTypes;
            std::unordered_map<clang::ClassTemplateDecl*, ClangTemplateClass*> ClangTemplateClasses;
            std::unordered_map<AST::FunctionOverloadSet*, OverloadSet*> OverloadSets;

            ClangCommonState ccs;

        public:
            void AddClangType(clang::QualType t, Type* match);

            Wide::Memory::Arena arena;

            Codegen::Generator* gen;

            Type* LiteralStringType;
            Type* Void;
            Type* Int8;
            Type* Boolean;

            
            // The contract of this function is to return the Wide type that corresponds to that Clang type.
            // Not to return a ClangType instance.
            Type* GetClangType(ClangUtil::ClangTU& from, clang::QualType t);
            ClangNamespace* GetClangNamespace(ClangUtil::ClangTU& from, clang::DeclContext* dc);
            FunctionType* GetFunctionType(Type* ret, const std::vector<Type*>& t);
            Module* GetWideModule(AST::Module* m);
            Function* GetWideFunction(AST::Function* p);
            LvalueType* GetLvalueType(Type* t);
            RvalueType* GetRvalueType(Type* t);
            ConstructorType* GetConstructorType(Type* t);
            ClangTemplateClass* GetClangTemplateClass(ClangUtil::ClangTU& from, clang::ClassTemplateDecl*);
            OverloadSet* GetOverloadSet(AST::FunctionOverloadSet* set);
            
            Expression AnalyzeExpression(Type* t, AST::Expression* e);
            Expression LookupIdentifier(AST::ModuleLevelDeclaration* decl, std::string ident);

            Analyzer(const Options::Clang&, Codegen::Generator*);     

            ClangUtil::ClangTU* LoadCPPHeader(std::string file);  

            void operator()(AST::Module*);
            ~Analyzer();
        };
    }
}
