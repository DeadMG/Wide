#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Semantic.h>
#include <Wide/SemanticTest/MockCodeGenerator.h>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

extern "C" __declspec(dllexport) void AnalyzeWide(
    Wide::AST::Combiner* comb,
    std::add_pointer<void(CEquivalents::Range, Wide::Semantic::Error, void*)>::type errorcallback,
    void* context
) {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    Wide::Options::Clang clangopts;
    clangopts.TargetOptions.Triple = "i686-pc-mingw32";
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(clangopts.TargetOptions.Triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(clangopts.TargetOptions.Triple, llvm::Triple(clangopts.TargetOptions.Triple).getArchName(), "", targetopts));
    Wide::Codegen::MockGenerator mockgen(*targetmachine->getDataLayout());
    try {
        Wide::Semantic::Analyze(comb->GetGlobalModule(), clangopts, mockgen);
    } catch(Wide::Semantic::SemanticError& err) {
        errorcallback(err.location(), err.error(), context);
    } catch(...) {
    }
}

extern "C" __declspec(dllexport) const char* GetAnalyzerErrorString(Wide::Semantic::Error err) {
    auto&& strings = Wide::Semantic::ErrorStrings;
    if (strings.find(err) != strings.end())
        return strings.at(err).c_str();
    return "ICE: Could not locate semantic error string.";
}