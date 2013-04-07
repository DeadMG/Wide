#define _SCL_SECURE_NO_WARNINGS

#include "Semantic/Analyzer.h"
#include "Parser/Builder.h"
#include "Codegen/Generator.h"
#include "ClangOptions.h"
#include "LLVMOptions.h"
#include <string>
#include <unordered_map>

namespace llvm {
    class LLVMContext;
}

namespace Wide {
    class Library {
        Codegen::Generator Generator;
        AST::Builder ASTBuilder;
        Semantic::Analyzer Sema;
    public:
        Library(const Options::Clang& clangopts, const Options::LLVM& llvmopts);
        void AddWideFile(std::string filename);        
        void operator()();
    };
}