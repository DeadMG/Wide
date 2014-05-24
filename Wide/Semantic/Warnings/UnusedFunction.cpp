#include <Wide/Semantic/Warnings/UnusedFunction.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

std::string GetFunctionName(const AST::FunctionBase* func, Analyzer& a, std::string context, Wide::Semantic::Module* root) {
    context += "(";
    for (auto&& arg : func->args) {
        if (arg.type) {
            struct autotype : public Wide::Semantic::MetaType {
                autotype(Type* con) : context(con), MetaType(con->analyzer) {}
                std::unique_ptr<ConstructorType> conty;
                std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> self, std::string name, Context c) override final {
                    if (name == "auto") {
                        if (!conty) conty = Wide::Memory::MakeUnique<ConstructorType>(this, analyzer);
                        return conty->BuildValueConstruction(Expressions(), c);
                    }
                    return nullptr;
                }
                Type* context;
                Type* GetContext() override final {
                    return context;
                }
                std::string explain() {
                    return "auto";
                }
            };
            try {
                auto autoty = autotype(root);
                auto expr = AnalyzeExpression(&autoty, arg.type, a);
                auto&& conty = dynamic_cast<ConstructorType&>(*expr->GetType()->Decay());
                context += (conty.GetConstructedType()->explain() + " ");
            } catch (...) {
                context += "error-type ";
            }
        }
        context += arg.name;
        if (&arg != &func->args.back())
            context += ", ";
    }
    return context + ")";
}
void GetUnusedFunctionsInModule(std::vector<std::tuple<const AST::FunctionBase*, Lexer::Range, std::string>>& current, Wide::Semantic::Module* root, Analyzer& a, std::string content) {
    for (auto&& decl : root->GetASTModule()->decls) {
        if (auto overset = dynamic_cast<const AST::FunctionOverloadSet*>(decl.second)) {
            for (auto&& func : overset->functions) {
                if (a.GetFunctions().find(func) == a.GetFunctions().end())
                    current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + decl.first, root)));
            }            
        }
        if (auto mod = dynamic_cast<const AST::Module*>(decl.second)) {
            GetUnusedFunctionsInModule(current, a.GetWideModule(mod, root), a, content + "." + decl.first);
        }
        if (auto udt = dynamic_cast<const AST::Type*>(decl.second)) {
            for (auto&& funcs : udt->Functions) {
                for (auto&& func : funcs.second->functions)
                    if (a.GetFunctions().find(func) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + funcs.first, root)));
            }
            for (auto&& funcs : udt->opcondecls) {
                for (auto&& func : funcs.second->functions)
                    if (a.GetFunctions().find(func) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(funcs.first), root)));
            }
        }
        if (auto temp = dynamic_cast<const AST::TemplateTypeOverloadSet*>(decl.second)) {
            for (auto tempty : temp->templatetypes) {
                auto udt = tempty->t;
                for (auto&& funcs : udt->Functions) {
                    for (auto&& func : funcs.second->functions)
                        if (a.GetFunctions().find(func) == a.GetFunctions().end())
                            current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + funcs.first, root)));
                }
                for (auto&& funcs : udt->opcondecls) {
                    for (auto&& func : funcs.second->functions)
                        if (a.GetFunctions().find(func) == a.GetFunctions().end())
                            current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(funcs.first), root)));
                }
            }
        }
    }
    for (auto decl : root->GetASTModule()->opcondecls) {
        for (auto func : decl.second->functions) {
            if (a.GetFunctions().find(func) == a.GetFunctions().end())
                current.push_back(std::make_tuple(func, func->where, GetFunctionName(func, a, content + "." + Wide::Semantic::GetNameForOperator(decl.first), root)));
        }
    }
}
std::vector<std::tuple<const AST::FunctionBase*, Lexer::Range, std::string>> Semantic::GetUnusedFunctions(Analyzer& a) {
    std::vector<std::tuple<const AST::FunctionBase*, Lexer::Range, std::string>> functions;
    GetUnusedFunctionsInModule(functions, a.GetGlobalModule(), a, "");
    return functions;
}