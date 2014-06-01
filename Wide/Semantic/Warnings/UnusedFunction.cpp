#include <Wide/Semantic/Warnings/UnusedFunction.h>
#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

void GetUnusedFunctionsInModule(std::vector<std::tuple<Lexer::Range, std::string>>& current, Wide::Semantic::Module* root, Analyzer& a, std::string content) {
    for (auto&& decl : root->GetASTModule()->decls) {
        if (auto overset = dynamic_cast<const AST::FunctionOverloadSet*>(decl.second)) {
            for (auto&& func : overset->functions) {
                if (a.GetFunctions().find(func) == a.GetFunctions().end())
                    current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + decl.first, root)));
            }            
        }
        if (auto mod = dynamic_cast<const AST::Module*>(decl.second)) {
            GetUnusedFunctionsInModule(current, a.GetWideModule(mod, root), a, content + "." + decl.first);
        }
        if (auto udt = dynamic_cast<const AST::Type*>(decl.second)) {
            for (auto&& funcs : udt->Functions) {
                for (auto&& func : funcs.second->functions)
                    if (a.GetFunctions().find(func) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + funcs.first, root)));
            }
            for (auto&& funcs : udt->opcondecls) {
                for (auto&& func : funcs.second->functions)
                    if (a.GetFunctions().find(func) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(funcs.first), root)));
            }
        }
        if (auto temp = dynamic_cast<const AST::TemplateTypeOverloadSet*>(decl.second)) {
            for (auto tempty : temp->templatetypes) {
                auto udt = tempty->t;
                for (auto&& funcs : udt->Functions) {
                    for (auto&& func : funcs.second->functions)
                        if (a.GetFunctions().find(func) == a.GetFunctions().end())
                            current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + funcs.first, root)));
                }
                for (auto&& funcs : udt->opcondecls) {
                    for (auto&& func : funcs.second->functions)
                        if (a.GetFunctions().find(func) == a.GetFunctions().end())
                            current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(funcs.first), root)));
                }
            }
        }
    }
    for (auto decl : root->GetASTModule()->opcondecls) {
        for (auto func : decl.second->functions) {
            if (a.GetFunctions().find(func) == a.GetFunctions().end())
                current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(decl.first), root)));
        }
    }
}
std::vector<std::tuple<Lexer::Range, std::string>> Semantic::GetUnusedFunctions(Analyzer& a) {
    std::vector<std::tuple<Lexer::Range, std::string>> functions;
    GetUnusedFunctionsInModule(functions, a.GetGlobalModule(), a, "");
    return functions;
}