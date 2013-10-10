#include <Wide/Semantic/Semantic.h>
#include <Wide/Semantic/Analyzer.h>

void Wide::Semantic::Analyze(const AST::Module* root, const Options::Clang& opts, Codegen::Generator& g) {
    Analyzer(opts, &g)(root);
}