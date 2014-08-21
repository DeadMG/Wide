#include <Wide/Semantic/Warnings/UnusedFunction.h>
#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

void GetUnusedFunctionsInModule(std::vector<std::tuple<Lexer::Range, std::string>>& current, Wide::Semantic::Module* root, Analyzer& a, std::string content) {
    for (auto&& decl : root->GetASTModule()->named_decls) {
        if (auto overset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Parse::Function*>>>(&decl.second)) {
            for (auto&& set : *overset) {
                for (auto func : set.second)
                    if (a.GetFunctions().find(func) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + decl.first, root)));
            }            
        }
        if (auto mod = boost::get<std::pair<Parse::Access, Parse::Module*>>(&decl.second)) {
            GetUnusedFunctionsInModule(current, a.GetWideModule(mod->second, root), a, content + "." + decl.first);
        }
        auto ProcessType = [&](Parse::Type* ty) {
            for (auto&& funcs : ty->nonvariables)
                if (auto overset = boost::get<Parse::OverloadSet<Parse::Function>*>(funcs.second))
                    for (auto&& access : *overset)
                        for (auto&& func : access.second)
                            if (a.GetFunctions().find(func) == a.GetFunctions().end())
                                current.push_back(std::make_tuple(func->where, GetFunctionName(func, a, content + "." + GetNameAsString(funcs.first), root)));
            for (auto func : ty->constructor_decls)
                for (auto con : func.second)
                    if (a.GetFunctions().find(con) == a.GetFunctions().end())
                        current.push_back(std::make_tuple(con->where, GetFunctionName(con, a, content + ".type", root)));
            if (ty->destructor_decl)
                if (a.GetFunctions().find(ty->destructor_decl) == a.GetFunctions().end())
                    current.push_back(std::make_tuple(ty->destructor_decl->where, GetFunctionName(ty->destructor_decl, a, content + ".~type", root)));
        };
        if (auto udt = boost::get<std::pair<Parse::Access, Parse::Type*>>(&decl.second)) {
            ProcessType(udt->second);
        }
        if (auto overset = boost::get<std::unordered_map<Parse::Access, std::unordered_set<Parse::TemplateType*>>>(&decl.second)) {
            for (auto&& funcs : (*overset))
                for (auto tempty : funcs.second)
                    ProcessType(tempty->t);
        }
    }
    for (auto&& decl : root->GetASTModule()->constructor_decls)
        if (a.GetFunctions().find(decl) == a.GetFunctions().end())
            current.push_back(std::make_tuple(decl->where, content + ".type"));
    for (auto&& decl : root->GetASTModule()->constructor_decls)
        if (a.GetFunctions().find(decl) == a.GetFunctions().end())
            current.push_back(std::make_tuple(decl->where, content + ".~type"));
}
std::vector<std::tuple<Lexer::Range, std::string>> Semantic::GetUnusedFunctions(Analyzer& a) {
    std::vector<std::tuple<Lexer::Range, std::string>> functions;
    GetUnusedFunctionsInModule(functions, a.GetGlobalModule(), a, "");
    return functions;
}