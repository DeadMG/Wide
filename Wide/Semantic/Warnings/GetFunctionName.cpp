#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

std::string Wide::Semantic::GetFunctionName(const Parse::FunctionBase* func, Analyzer& a, std::string context, Wide::Semantic::Module* root) {
    context += "(";
    for (auto&& arg : func->args) {
        context += arg.name;
        if (arg.type) {
            try {
                auto expr = a.AnalyzeExpression(root, arg.type);
                auto&& conty = dynamic_cast<ConstructorType&>(*expr->GetType()->Decay());
                context += " := " + conty.GetConstructedType()->explain();
            }
            catch (...) {
                context += ":= error-type";
            }
        }
        if (&arg != &func->args.back())
            context += ", ";
    }
    return context + ")";
}