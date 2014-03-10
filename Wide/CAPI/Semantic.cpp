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
    p->LanguageOptions.MicrosoftExt = true;
    p->LanguageOptions.MicrosoftMode = true;
    p->LanguageOptions.MSCVersion = 1800;
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

enum class ContextType {
    Unknown,
    Module,
    Type,
    OverloadSet
};

extern "C" DLLEXPORT void AnalyzeWide(
    CEquivalents::Combiner* comb,
    Wide::Options::Clang* clangopts,
    std::add_pointer<void(CEquivalents::Range, const char* what, void* context)>::type error,
    std::add_pointer<void(CEquivalents::Range, const char* what, ContextType type, void* context)>::type quickinfo,
    std::add_pointer<void(CEquivalents::Range, void* context)>::type paramhighlight,
    void* context
) {
    Wide::Driver::NullGenerator mockgen(clangopts->TargetOptions.Triple);
    Wide::Semantic::Analyzer a(*clangopts, mockgen, comb->combiner.GetGlobalModule());
    a.QuickInfo = [&](Wide::Lexer::Range r, Wide::Semantic::Type* t) {
        ContextType cty = ContextType::Unknown;
        if (dynamic_cast<Wide::Semantic::Module*>(t->Decay())) {
            cty = ContextType::Module;
        }
        if (dynamic_cast<Wide::Semantic::ConstructorType*>(t->Decay())) {
            cty = ContextType::Type;
        }
        if (dynamic_cast<Wide::Semantic::OverloadSet*>(t->Decay())) {
            cty = ContextType::OverloadSet;
        }
        quickinfo(r, t->explain(a).c_str(), cty, context);
    };
    a.ParameterHighlight = [&](Wide::Lexer::Range r) {
        paramhighlight(r, context);
    };
    Wide::Test::Test(a, nullptr, comb->combiner.GetGlobalModule(), [&](Wide::Semantic::Error& e) { 
            error(e.location(), e.what(), context); 
    }, mockgen);
}