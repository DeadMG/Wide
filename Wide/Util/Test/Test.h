#pragma once

#include <Wide/Parser/AST.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ConstructorType.h>

namespace Wide {
    namespace Test {
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
            auto swallow_raii = [](Wide::Semantic::ConcreteExpression e) {};
            auto mod = a.GetWideModule(root, higher);
            for(auto overset : root->functions) {
                for(auto fun : overset.second->functions) {
                    Wide::Semantic::Context c(a, fun->where(), swallow_raii, mod);
                    if (fun->args.size() == 0) {
                        Try([&] { a.GetWideFunction(fun, mod)->BuildCall(a.GetWideFunction(fun, mod)->BuildValueConstruction(c), std::vector<Wide::Semantic::ConcreteExpression>(), c); }, errorfunc, swallow);
                        continue;
                    }
                    std::vector<Wide::Semantic::ConcreteExpression> concexpr;
                    try {
                        for(auto arg : fun->args) {
                            if (!arg.type)
                                continue;
                            Try([&] {
                                auto ty = a.AnalyzeExpression(mod, arg.type, swallow_raii);
                                if (auto conty = dynamic_cast<Wide::Semantic::ConstructorType*>(ty.t->Decay())) 
                                    concexpr.push_back(Wide::Semantic::ConcreteExpression(conty->GetConstructedType(), mockgen.CreateNop()));
                            }, errorfunc, swallow);
                        }
                        if (concexpr.size() == fun->args.size()) {
                            std::vector<Wide::Semantic::Type*> types;
                            for(auto x : concexpr)
                                types.push_back(x.t);
                            Try([&] { 
                                auto ty = a.GetWideFunction(fun, mod, std::move(types));
                                ty->BuildCall(ty->BuildValueConstruction(c), std::move(concexpr), c); 
                            }, errorfunc, swallow);
                        }
                    } catch(...) {}
                }
            }
            for(auto decl : root->decls) {
                Wide::Semantic::Context c(a, root->where.front(), swallow_raii, mod);
                if (auto use = dynamic_cast<Wide::AST::Using*>(decl.second)) {
                    Try([&] { mod->BuildValueConstruction(c).AccessMember(decl.first, c); }, errorfunc, swallow);
                }
                if (auto module = dynamic_cast<Wide::AST::Module*>(decl.second)) {
                    Try([&] { mod->BuildValueConstruction(c).AccessMember(decl.first, c); }, errorfunc, swallow);
                    Test(a, mod, module, errorfunc, mockgen, swallow);
                }
            }
        }
    }
}