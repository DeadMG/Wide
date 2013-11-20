#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Util/Driver/NullCodeGenerator.h>
#include <Wide/Util/Test/Test.h>
#include <Wide/Util/DebugUtilities.h>
#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

std::unordered_set<Wide::Options::Clang*> validopts;

extern "C" DLLEXPORT Wide::Options::Clang* CreateClangOptions(const char* triple) {
    auto p = new Wide::Options::Clang();
    p->TargetOptions.Triple = triple;
    validopts.insert(p);
    return p;
}

extern "C" DLLEXPORT void DestroyClangOptions(Wide::Options::Clang* p) {
    if (validopts.find(p) == validopts.end())
        Wide::Util::DebugBreak();
    validopts.erase(p);
    delete p;
}

extern "C" DLLEXPORT void AddHeaderPath(Wide::Options::Clang* p, const char* path, bool angled) {
    p->HeaderSearchOptions->AddPath(path, angled ? clang::frontend::IncludeDirGroup::Angled : clang::frontend::IncludeDirGroup::Quoted, false, false);
}

extern "C" DLLEXPORT void AnalyzeWide(
    Wide::AST::Combiner* comb,
    Wide::Options::Clang* clangopts,
    std::add_pointer<void(CEquivalents::Range, Wide::Semantic::Error, void*)>::type errorcallback,
    void* context
) {
    Wide::Driver::NullGenerator mockgen(clangopts->TargetOptions.Triple);
    Wide::Semantic::Analyzer a(*clangopts, mockgen, comb->GetGlobalModule());
    Wide::Test::Test(a, nullptr, comb->GetGlobalModule(), [&](CEquivalents::Range r, Wide::Semantic::Error e) { errorcallback(r, e, context); }, mockgen, true);
}

extern "C" DLLEXPORT const char* GetAnalyzerErrorString(Wide::Semantic::Error err) {
    auto&& strings = Wide::Semantic::ErrorStrings;
    if (strings.find(err) != strings.end())
        return strings.at(err).c_str();
    return "ICE: Could not locate semantic error string.";
}
