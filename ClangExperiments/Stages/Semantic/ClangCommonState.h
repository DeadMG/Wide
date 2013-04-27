#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include "../../Util/MemoryArena.h"
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
    namespace Semantic {
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
    }
}