#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ConstructorType.h>

template<typename F, typename T> void Try(F&& t, T errorcallback, bool swallow) {
    try {
        t();
    } catch(Wide::Semantic::SemanticError& err) {
         errorcallback(err.location(), err.error());
    } catch(...) {
        if (!swallow)
            throw;
    }
}

template<typename T, typename U> void Test(Wide::Semantic::Analyzer& a, Wide::Semantic::Module* higher, const Wide::AST::Module* root, T errorfunc, U& mockgen, bool swallow) {
    auto mod = a.GetWideModule(root, higher);
    for(auto overset : root->functions) {
        for(auto fun : overset.second->functions) {
            if (fun->args.size() == 0) {
                Try([&] { a.GetWideFunction(fun, mod)->BuildCall(Wide::Semantic::ConcreteExpression(), std::vector<Wide::Semantic::ConcreteExpression>(), a, fun->where.front()); }, errorfunc, swallow);
                continue;
            }
            std::vector<Wide::Semantic::ConcreteExpression> concexpr;
            try {
                for(auto arg : fun->args) {
                    if (!arg.type)
                        continue;
                    Try([&] {
                        auto ty = a.AnalyzeExpression(mod, arg.type);
                        if (auto conty = dynamic_cast<Wide::Semantic::ConstructorType*>(ty.Resolve(nullptr).t->Decay())) 
                            concexpr.push_back(Wide::Semantic::ConcreteExpression(conty->GetConstructedType(), mockgen.CreateNop()));
                    }, errorfunc, swallow);
                }
                if (concexpr.size() == fun->args.size()) {
                    std::vector<Wide::Semantic::Type*> types;
                    for(auto x : concexpr)
                        types.push_back(x.t);
                    Try([&] { a.GetWideFunction(fun, mod, std::move(types))->BuildCall(Wide::Semantic::ConcreteExpression(), std::move(concexpr), a, fun->where.front()); }, errorfunc, swallow);
                }
            } catch(...) {}
        }
    }
    for(auto decl : root->decls) {
        if (auto use = dynamic_cast<Wide::AST::Using*>(decl.second)) {
            Try([&] { mod->BuildValueConstruction(a, root->where.front()).AccessMember(use->name, a, root->where.front()); }, errorfunc, swallow);
        }
        if (auto module = dynamic_cast<Wide::AST::Module*>(decl.second)) {
            Try([&] { mod->BuildValueConstruction(a, root->where.front()).AccessMember(module->name, a, root->where.front()); }, errorfunc, swallow);
            Test(a, mod, module, errorfunc, mockgen, swallow);
        }
    }
}