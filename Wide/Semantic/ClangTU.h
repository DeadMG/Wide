#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Hashers.h>
#include <memory>
#include <string>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/Basic/ABI.h>
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
    class CXXDestructorDecl;
    class CXXConstructorDecl;
    class VarDecl;
    namespace CodeGen {
        class CodeGenModule;
        class CGFunctionInfo;
    }
}

namespace Wide {
    namespace Semantic {
        class Analyzer;
        class ClangTU {
            class Impl;
            std::unordered_set<clang::FunctionDecl*> visited;
            void MarkDecl(clang::NamedDecl* decl);
            Analyzer& a;
        public:
            std::unique_ptr<Impl> impl;
            ~ClangTU();
            void GenerateCodeAndLinkModule(llvm::Module* module, llvm::DataLayout&, Analyzer& a);
            clang::DeclContext* GetDeclContext();

            ClangTU(ClangTU&&);

            ClangTU(std::string file, const Wide::Options::Clang&, Lexer::Range where, Analyzer& an);
            llvm::Type* GetLLVMTypeFromClangType(clang::QualType t, llvm::Module* module);
            // Absolute mapped to user-provided.
            std::unordered_map<std::string, std::string> GetFilename();

            clang::ASTContext& GetASTContext();
            clang::Sema& GetSema();
            clang::IdentifierInfo* GetIdentifierInfo(std::string ident);
            clang::SourceLocation GetFileEnd();
            std::string PopLastDiagnostic();
            void AddFile(std::string filename, Lexer::Range where);
            clang::Expr* ParseMacro(std::string macro, Lexer::Range where);

            std::function<llvm::Function*(llvm::Module*)> GetObject(Analyzer& ana, clang::CXXDestructorDecl*, clang::CXXDtorType);
            std::function<llvm::Function*(llvm::Module*)> GetObject(Analyzer& ana, clang::CXXConstructorDecl*, clang::CXXCtorType);
            std::function<llvm::Function*(llvm::Module*)> GetObject(Analyzer& ana, clang::CXXConstructorDecl*) = delete;
            std::function<llvm::Function*(llvm::Module*)> GetObject(Analyzer& ana, clang::CXXDestructorDecl*) = delete;

            std::function<llvm::GlobalVariable*(llvm::Module*)> GetObject(Analyzer& ana, clang::VarDecl*);
            std::function<llvm::Function*(llvm::Module*)> GetObject(Analyzer& ana, clang::FunctionDecl*);

            bool IsComplexType(clang::CXXRecordDecl* decl, llvm::Module* module);
            std::function<unsigned(llvm::Module*)> GetFieldNumber(clang::FieldDecl*);
            std::function<unsigned(llvm::Module*)> GetBaseNumber(const clang::CXXRecordDecl* self, const clang::CXXRecordDecl* base);
            llvm::Constant* GetItaniumRTTI(clang::QualType, llvm::Module* m);
            unsigned int GetVirtualFunctionOffset(clang::CXXMethodDecl*, llvm::Module* module);

            llvm::PointerType* GetFunctionPointerType(const clang::CodeGen::CGFunctionInfo&, llvm::Module* module);
            const clang::CodeGen::CGFunctionInfo& GetABIForFunction(const clang::FunctionProtoType*, clang::CXXRecordDecl*, llvm::Module* module);
            const clang::CodeGen::CGFunctionInfo& GetABIForFunction(clang::CXXConstructorDecl*, llvm::Module* module) = delete;
            const clang::CodeGen::CGFunctionInfo& GetABIForFunction(clang::CXXDestructorDecl*, llvm::Module* module) = delete;
            const clang::CodeGen::CGFunctionInfo& GetABIForFunction(clang::CXXDestructorDecl*, clang::CXXDtorType, llvm::Module* module);
            const clang::CodeGen::CGFunctionInfo& GetABIForFunction(clang::CXXConstructorDecl*, clang::CXXCtorType, llvm::Module* module);
            clang::CodeGen::CodeGenModule& GetCodegenModule(llvm::Module* module);
            clang::SourceLocation GetLocationForRange(Lexer::Range r);
        };
    }
}