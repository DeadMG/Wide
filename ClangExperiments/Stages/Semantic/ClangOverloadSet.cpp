#include "ClangOverloadSet.h"
#include "../../Util/MakeUnique.h"
#include "../Codegen/Generator.h"
#include "../Codegen/Expression.h"
#include "ClangTU.h"
#include "Util.h"
#include "ConstructorType.h"
#include "FunctionType.h"
#include "Analyzer.h"

#pragma warning(push, 0)

#include <clang/Sema/Sema.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

ClangOverloadSet::ClangOverloadSet(std::unique_ptr<clang::UnresolvedSet<8>> arg, ClangUtil::ClangTU* tu, Type* t)
    : lookupset(std::move(arg)), from(tu), nonstatic(t), templateargs(nullptr) {}

Expression ClangOverloadSet::BuildCallWithTemplateArguments(clang::TemplateArgumentListInfo* templateargs, Expression mem, std::vector<Expression> args, Analyzer& a) {
    std::vector<clang::OpaqueValueExpr> exprs;
    if (nonstatic) {
        // The mem's type will be this, even though we don't actually want that. Set it back to the original type.
        mem.t = nonstatic;
        args.insert(args.begin(), mem);
        //exprs.push_back(Wide::Util::make_unique<clang::OpaqueValueExpr>(clang::SourceLocation(), nonstatic->GetClangType(*from).getNonLValueExprType(from->GetASTContext()), GetKindOfType(nonstatic)));
    }
    for(auto x : args) {
        exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x.t->GetClangType(*from).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x.t)));
    }
    clang::OverloadCandidateSet s((clang::SourceLocation()));
    std::vector<clang::Expr*> exprptrs;
    for(auto&& x : exprs) {
        exprptrs.push_back(&x);
    }
    from->GetSema().AddFunctionCandidates(*lookupset, exprptrs, s, false, templateargs);
    clang::OverloadCandidateSet::iterator best;
    auto result = s.BestViableFunction(from->GetSema(), clang::SourceLocation(), best);
    if (result == clang::OverloadingResult::OR_Ambiguous)
        throw std::runtime_error("Attempted to make an overloaded call, but Clang said the overloads were ambiguous.");
    if (result == clang::OverloadingResult::OR_Deleted)
        throw std::runtime_error("Attempted to make an overloaded call, but Clang said the best match was a C++ deleted function.");
    if (result == clang::OverloadingResult::OR_No_Viable_Function)
        throw std::runtime_error("Attempted to make an overloaded call, but Clang said that there were no viable functions.");
    auto fun = best->Function;
    std::vector<Type*> types;
    if (nonstatic)
        types.push_back(nonstatic);
    for(unsigned i = 0; i < fun->getNumParams(); ++i) {
        types.push_back(a.GetClangType(*from, fun->getParamDecl(i)->getType()));
    }
    auto funty = a.GetFunctionType(a.GetClangType(*from, fun->getResultType()), types);
    Expression obj;
    obj.t = funty;
    obj.Expr = a.gen->CreateFunctionValue(from->MangleName(fun));
    auto ret = funty->BuildCall(obj, args, a);
    return ret;
}

Expression ClangOverloadSet::BuildCall(Expression mem, std::vector<Expression> args, Analyzer& a) {
    return BuildCallWithTemplateArguments(nullptr, mem, std::move(args), a);
}

Expression ClangOverloadSet::BuildMetaCall(Expression val, std::vector<Expression> args, Analyzer& a) {
    struct TemplateOverloadSet : public Type {
        clang::TemplateArgumentListInfo tempargs;
        ClangOverloadSet* set;
        Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
            return set->BuildCallWithTemplateArguments(&tempargs, val, std::move(args), a);
        }
    };
    Expression out;
    auto tset = a.arena.Allocate<TemplateOverloadSet>();
    tset->set = this;    
    for(auto&& x : args) {
        auto con = dynamic_cast<ConstructorType*>(x.t);
        if (!con)
            throw std::runtime_error("Only support types as arguments to Clang templates right now.");
        auto clangty = con->GetConstructedType()->GetClangType(*from);

        auto tysrcinfo = from->GetASTContext().getTrivialTypeSourceInfo(clangty);
        
        tset->tempargs.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(clangty), tysrcinfo));
    }
    out.Expr = val.Expr;
    out.t = tset;
    return out;
}