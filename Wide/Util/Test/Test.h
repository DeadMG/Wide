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
        template<typename F, typename T> bool Try(F&& t, T errorcallback) {
            try {
                t();
            } catch(Wide::Semantic::Error& err) {
                 errorcallback(err);
                 return true;
            } catch (std::runtime_error& e) {
                return true;
            }
            return false;
        }
        
        template<typename T, typename U> bool Test(Wide::Semantic::Analyzer& a, Wide::Semantic::Module* higher, const Wide::AST::Module* root, T errorfunc, U& mockgen) {
            bool result = false;
            auto swallow_raii = [](Wide::Semantic::ConcreteExpression e) {};
            auto mod = a.GetWideModule(root, higher);
            for(auto decl : root->decls) {
                Wide::Semantic::Context c(a, root->where.front(), swallow_raii, mod);
                if (auto use = dynamic_cast<Wide::AST::Using*>(decl.second)) {
                    result = result | Try([&] { mod->BuildValueConstruction({}, c).AccessMember(decl.first, c); }, errorfunc);
                }
                if (auto module = dynamic_cast<Wide::AST::Module*>(decl.second)) {
                    result = result | Try([&] { mod->BuildValueConstruction({}, c).AccessMember(decl.first, c); }, errorfunc);
                    result = result | Test(a, mod, module, errorfunc, mockgen);
                }
                if (auto overset = dynamic_cast<Wide::AST::FunctionOverloadSet*>(decl.second)) {
                    for (auto fun : overset->functions) {
                        if (Wide::Semantic::IsMultiTyped(fun))
                            continue;
                        Wide::Semantic::Context c(a, fun->where, swallow_raii, mod);
                        if (fun->args.size() == 0) {
                            result = result | Try([&] { a.GetWideFunction(fun, mod, std::vector<Wide::Semantic::Type*>(), decl.first)->ComputeBody(a); }, errorfunc);
                            continue;
                        }
                        std::vector<Wide::Semantic::Type*> types;
                        for (auto arg : fun->args) {
                            result = result | Try([&] {
                                auto ty = a.AnalyzeExpression(mod, arg.type, swallow_raii);
                                if (auto conty = dynamic_cast<Wide::Semantic::ConstructorType*>(ty.t->Decay()))
                                    types.push_back(conty->GetConstructedType());
                            }, errorfunc);
                        }
                        if (types.size() == fun->args.size()) {
                            result = result | Try([&] {
                                auto ty = a.GetWideFunction(fun, mod, std::move(types), decl.first);
                                ty->ComputeBody(a);
                            }, errorfunc);
                        }
                    }
                }
            }
            return result;
        }
    }
}