#include <Wide/Semantic/Semantic.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Type.h>

void Wide::Semantic::Analyze(const AST::Module* root, const Options::Clang& opts, Codegen::Generator& g) {
    static const Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
    Analyzer a(opts, &g, root);
    auto std = a.GetGlobalModule()->AccessMember("Standard", a, location);
    if (!std)
        throw std::runtime_error("Fuck.");
    auto main = std->Resolve(nullptr).t->AccessMember("Main", a, location);
    if (!main)
        throw std::runtime_error("Fuck.");
    main->Resolve(nullptr).BuildCall(a, root->decls.at("Standard")->where.front());
}