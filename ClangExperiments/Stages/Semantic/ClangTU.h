#pragma once

#define _SCL_SECURE_NO_WARNINGS 

#include <memory>
#include <string>
#include <functional>
#include "Analyzer.h"

namespace llvm {
    class Module;
    class LLVMContext;
    class Type;
    class Constant;
}

namespace clang {
    class DeclContext;
    class ASTContext;
    class Sema;
    class QualType;
    class NamedDecl;
    class IdentifierInfo;
    class GlobalDecl;
    class FieldDecl;
    class SourceLocation;
}

namespace Wide {
    namespace Semantic {        
        struct ClangTypeHasher {
            std::size_t operator()(clang::QualType t) const;
        };
    };
    namespace ClangUtil {
        class ClangTU {
            class Impl;
        public:
            std::unique_ptr<Impl> impl;
            ~ClangTU();
            void GenerateCodeAndLinkModule(llvm::Module* main);
            clang::DeclContext* GetDeclContext();

            ClangTU(ClangTU&&);

            ClangTU(llvm::LLVMContext& c, std::string file, Semantic::ClangCommonState&);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMTypeFromClangType(clang::QualType t);
            std::string MangleName(clang::NamedDecl* D);

            clang::ASTContext& GetASTContext();
            clang::Sema& GetSema();
            clang::IdentifierInfo* GetIdentifierInfo(std::string ident);
            llvm::Constant* GetAddrOfGlobal(clang::GlobalDecl GD);
            unsigned GetFieldNumber(clang::FieldDecl*);
            clang::SourceLocation GetFileEnd();
        };
    }
}