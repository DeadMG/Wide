#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Hashers.h>
#include <Wide/Codegen/Generator.h>
#include <memory>
#include <string>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)

namespace llvm {
    class Module;
    class LLVMContext;
    class Type;
    class Constant;
}

namespace clang {
    class DeclContext;
    class ASTContext;
    class Expr; 
    class Sema;
    class QualType;
    class NamedDecl;
    class IdentifierInfo;
    class GlobalDecl;
    class FieldDecl;
    class SourceLocation;
    class CXXRecordDecl;
    class CXXMethodDecl;
    class FunctionDecl;
}

namespace Wide {
    namespace Semantic {        
        class Analyzer;
        class ClangTU {
            class Impl;
            std::unordered_set<clang::FunctionDecl*> visited;
            Analyzer& a;
        public:
            std::unique_ptr<Impl> impl;
            ~ClangTU();
            void GenerateCodeAndLinkModule(Codegen::Generator& g);
            clang::DeclContext* GetDeclContext();

            ClangTU(ClangTU&&);

            ClangTU(std::string file, const Wide::Options::Clang&, Lexer::Range where, Analyzer& an);
            llvm::Type* GetLLVMTypeFromClangType(clang::QualType t, Codegen::Generator& g);
            std::string GetFilename();

            std::function<std::string(Codegen::Generator&)> MangleName(clang::NamedDecl* D);
            bool IsComplexType(clang::CXXRecordDecl* decl, Codegen::Generator& g);
            clang::ASTContext& GetASTContext();
            clang::Sema& GetSema();
            clang::IdentifierInfo* GetIdentifierInfo(std::string ident);
            unsigned GetFieldNumber(clang::FieldDecl*, Codegen::Generator& g);
            unsigned GetBaseNumber(const clang::CXXRecordDecl* self, const clang::CXXRecordDecl* base, Codegen::Generator& g);
            clang::SourceLocation GetFileEnd();
            std::string PopLastDiagnostic();
            void AddFile(std::string filename, Lexer::Range where);
            clang::Expr* ParseMacro(std::string macro, Lexer::Range where);
            unsigned int GetVirtualFunctionOffset(clang::CXXMethodDecl*, Codegen::Generator& g);
        };
    }
}