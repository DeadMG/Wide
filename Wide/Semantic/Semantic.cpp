#include <Wide/Semantic/Semantic.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Type.h>

void Wide::Semantic::Analyze(const AST::Module* root, const Options::Clang& opts, Codegen::Generator& g) {
    Analyzer a(opts, &g);
    a(root);    
    auto std = a.GetWideModule(root)->AccessMember(ConcreteExpression(), "Standard", a);
    if (!std)
        throw std::runtime_error("Fuck.");
    auto main = std->t->AccessMember(ConcreteExpression(), "Main", a);
    if (!main)
        throw std::runtime_error("Fuck.");
    main->t->BuildCall(ConcreteExpression(), std::vector<ConcreteExpression>(), a, root->decls.at("Standard")->where.front());
}