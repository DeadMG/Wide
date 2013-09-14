#pragma once

#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/ClangOptions.h>
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
    class Sema;
    class QualType;
    class NamedDecl;
    class IdentifierInfo;
    class GlobalDecl;
    class FieldDecl;
    class SourceLocation;
    class CXXRecordDecl;
    class FunctionDecl;
}

namespace Wide {
    namespace Semantic {        
        class Analyzer;
        struct ClangTypeHasher {
            std::size_t operator()(clang::QualType t) const;
        };
    };
    namespace ClangUtil {
        class ClangTU {
            class Impl;
            std::unordered_set<clang::FunctionDecl*> visited;
            std::unordered_map<clang::QualType, std::function<llvm::Type*(llvm::Module*)>, ClangTypeHasher> prebaked_types;
        public:
            std::unique_ptr<Impl> impl;
            ~ClangTU();
            void GenerateCodeAndLinkModule(llvm::Module* main);
            clang::DeclContext* GetDeclContext();

            ClangTU(ClangTU&&);

            ClangTU(llvm::LLVMContext& c, std::string file, const Wide::Options::Clang&);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMTypeFromClangType(clang::QualType t, Semantic::Analyzer& a);
            std::string MangleName(clang::NamedDecl* D);

            bool IsComplexType(clang::CXXRecordDecl* decl);
            clang::ASTContext& GetASTContext();
            clang::Sema& GetSema();
            clang::IdentifierInfo* GetIdentifierInfo(std::string ident);
            unsigned GetFieldNumber(clang::FieldDecl*);
            clang::SourceLocation GetFileEnd();
        };
    }
}