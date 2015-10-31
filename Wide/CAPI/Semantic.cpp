#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/AST.h>
#include <Wide/CAPI/Parser.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Util/DebugBreak.h>
#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>

#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

/*
std::unordered_set<Wide::Options::Clang*> validopts;

extern "C" DLLEXPORT Wide::Options::Clang* CreateClangOptions(const char* triple) {
    auto p = new Wide::Options::Clang();
    p->TargetOptions.Triple = triple;
    p->LanguageOptions.MicrosoftExt = true;
    p->LanguageOptions.MSVCCompat = 1800;
    p->LanguageOptions.MSCompatibilityVersion = 1800;
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
    CEquivalents::Combiner* comb,
    Wide::Options::Clang* clangopts,
    std::add_pointer<void(CEquivalents::Range, const char* what, void* context)>::type error,
    std::add_pointer<void(CEquivalents::Range, const char* what, void* context)>::type quickinfo,
    std::add_pointer<void(CEquivalents::Range, void* context)>::type paramhighlight,
    void* context
) {
    llvm::LLVMContext con;
    Wide::Semantic::Analyzer a(*clangopts, comb->combiner.GetGlobalModule(), con);
    a.QuickInfo = [&](Wide::Lexer::Range r, Wide::Semantic::Type* t) {
        std::string content = t->explain();
        quickinfo(r, content.c_str(), context);
    };
    a.ParameterHighlight = [&](Wide::Lexer::Range r) {
        paramhighlight(r, context);
    };
    Wide::Semantic::AnalyzeExportedFunctions(a);
}*/