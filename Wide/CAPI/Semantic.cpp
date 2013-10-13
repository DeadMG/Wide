#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/SemanticTest/MockCodeGenerator.h>

#pragma warning(push, 0)
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

std::unordered_set<Wide::Options::Clang*> validopts;

extern "C" __declspec(dllexport) Wide::Options::Clang* CreateClangOptions(const char* triple) {
    auto p = new Wide::Options::Clang();
    p->TargetOptions.Triple = triple;
    validopts.insert(p);
    return p;
}

extern "C" __declspec(dllexport) void DestroyClangOptions(Wide::Options::Clang* p) {
    if (validopts.find(p) == validopts.end())
        __debugbreak();
    validopts.erase(p);
    delete p;
}

extern "C" __declspec(dllexport) void AddHeaderPath(Wide::Options::Clang* p, const char* path, bool angled) {
    p->HeaderSearchOptions->AddPath(path, angled ? clang::frontend::IncludeDirGroup::Angled : clang::frontend::IncludeDirGroup::Quoted, false, false);
}

template<typename F, typename T> void Try(F&& t, T errorcallback) {
    try {
        t();
    } catch(Wide::Semantic::SemanticError& err) {
         errorcallback(err.location(), err.error());
    } catch(...) {}
}

template<typename T, typename U> void Test(Wide::Semantic::Analyzer& a, const Wide::AST::Module* root, T errorfunc, U& mockgen) {
    for(auto overset : root->functions) {
        for(auto fun : overset.second->functions) {
            if (fun->args.size() == 0) {
                Try([&] { a.GetWideFunction(fun)->BuildCall(Wide::Semantic::ConcreteExpression(), std::vector<Wide::Semantic::ConcreteExpression>(), a, fun->where.front()); }, errorfunc);
                continue;
            }
            std::vector<Wide::Semantic::ConcreteExpression> concexpr;
            try {
                for(auto arg : fun->args) {
                    if (!arg.type)
                        continue;
                    Try([&] {
                        auto ty = a.AnalyzeExpression(a.GetDeclContext(fun->higher), arg.type);
                        if (auto conty = dynamic_cast<Wide::Semantic::ConstructorType*>(ty.Resolve(nullptr).t->Decay())) 
                            concexpr.push_back(Wide::Semantic::ConcreteExpression(conty->GetConstructedType(), mockgen.CreateNop()));
                    }, errorfunc);
                }
                if (concexpr.size() == fun->args.size()) {
                    std::vector<Wide::Semantic::Type*> types;
                    for(auto x : concexpr)
                        types.push_back(x.t);
                    Try([&] { a.GetWideFunction(fun, nullptr, std::move(types))->BuildCall(Wide::Semantic::ConcreteExpression(), std::move(concexpr), a, fun->where.front()); }, errorfunc);
                }
            } catch(...) {}
        }
    }
    for(auto decl : root->decls) {
        if (auto use = dynamic_cast<Wide::AST::Using*>(decl.second)) {
            Try([&] { a.GetWideModule(root)->BuildValueConstruction(a).AccessMember(use->name, a); }, errorfunc);
        }
        if (auto mod = dynamic_cast<Wide::AST::Module*>(decl.second)) {
            Try([&] { a.GetWideModule(root)->BuildValueConstruction(a).AccessMember(mod->name, a); }, errorfunc);
            Test(a, mod, errorfunc, mockgen);
        }
    }
}

extern "C" __declspec(dllexport) void AnalyzeWide(
    Wide::AST::Combiner* comb,
    Wide::Options::Clang* clangopts,
    std::add_pointer<void(CEquivalents::Range, Wide::Semantic::Error, void*)>::type errorcallback,
    void* context
) {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(clangopts->TargetOptions.Triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(clangopts->TargetOptions.Triple, llvm::Triple(clangopts->TargetOptions.Triple).getArchName(), "", targetopts));
    Wide::Codegen::MockGenerator mockgen(*targetmachine->getDataLayout());
    Wide::Semantic::Analyzer a(*clangopts, &mockgen);
    a(comb->GetGlobalModule());
    Test(a, comb->GetGlobalModule(), [&](CEquivalents::Range r, Wide::Semantic::Error e) { errorcallback(r, e, context); }, mockgen);
}

extern "C" __declspec(dllexport) const char* GetAnalyzerErrorString(Wide::Semantic::Error err) {
    auto&& strings = Wide::Semantic::ErrorStrings;
    if (strings.find(err) != strings.end())
        return strings.at(err).c_str();
    return "ICE: Could not locate semantic error string.";
}