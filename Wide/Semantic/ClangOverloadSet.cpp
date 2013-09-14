#include <Wide/Semantic/ClangOverloadSet.h>
#include <Wide/Util/MakeUnique.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <sstream>

#pragma warning(push, 0)
#include <clang/Sema/Sema.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

ClangOverloadSet::ClangOverloadSet(std::unique_ptr<clang::UnresolvedSet<8>> arg, ClangUtil::ClangTU* tu, Type* t)
    : lookupset(std::move(arg)), from(tu), nonstatic(t), templateargs(nullptr) {}

ConcreteExpression ClangOverloadSet::BuildCallWithTemplateArguments(clang::TemplateArgumentListInfo* templateargs, ConcreteExpression mem, std::vector<ConcreteExpression> args, Analyzer& a) {
    std::vector<clang::OpaqueValueExpr> exprs;
    if (nonstatic) {
        // The mem's type will be this, even though we don't actually want that. Set it back to the original type.
        mem = mem.BuildValue(a);
        mem.t = nonstatic;
        mem.Expr = a.gen->CreateFieldExpression(mem.Expr, 0);
        args.insert(args.begin(), mem);
        //exprs.push_back(Wide::Util::make_unique<clang::OpaqueValueExpr>(clang::SourceLocation(), nonstatic->GetClangType(*from).getNonLValueExprType(from->GetASTContext()), GetKindOfType(nonstatic)));
    }
    for(auto x : args) {
        exprs.push_back(clang::OpaqueValueExpr(clang::SourceLocation(), x.t->GetClangType(*from, a).getNonLValueExprType(from->GetASTContext()), GetKindOfType(x.t)));
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
    if (nonstatic) {
        types.push_back(nonstatic);
    }
    for(unsigned i = 0; i < fun->getNumParams(); ++i) {
        types.push_back(a.GetClangType(*from, fun->getParamDecl(i)->getType()));
    }
    return ConcreteExpression(a.GetFunctionType(a.GetClangType(*from, fun->getResultType()), types), a.gen->CreateFunctionValue(from->MangleName(fun))).BuildCall(args, a).Resolve(nullptr);
}

Expression ClangOverloadSet::BuildCall(ConcreteExpression mem, std::vector<ConcreteExpression> args, Analyzer& a) {
    return BuildCallWithTemplateArguments(nullptr, mem, std::move(args), a);
}

ConcreteExpression ClangOverloadSet::BuildMetaCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a) {
    struct TemplateOverloadSet : public Type {
        clang::TemplateArgumentListInfo tempargs;
        ClangOverloadSet* set;
        Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a) override {
            return set->BuildCallWithTemplateArguments(&tempargs, val, std::move(args), a);
        }
    };
    auto tset = a.arena.Allocate<TemplateOverloadSet>();
    tset->set = this;    
    for(auto&& x : args) {
        auto con = dynamic_cast<ConstructorType*>(x.t);
        if (!con)
            throw std::runtime_error("Only support types as arguments to Clang templates right now.");
        auto clangty = con->GetConstructedType()->GetClangType(*from, a);

        auto tysrcinfo = from->GetASTContext().getTrivialTypeSourceInfo(clangty);
        
        tset->tempargs.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(clangty), tysrcinfo));
    }
    return ConcreteExpression(tset, val.Expr);
}

std::function<llvm::Type*(llvm::Module*)> ClangOverloadSet::GetLLVMType(Analyzer& a) {
    // Have to cache result - not fun.
    auto g = a.gen;
    std::stringstream stream;
    stream << "struct.__" << this;
    auto str = stream.str();
    if (!nonstatic) {
        return [=](llvm::Module* m) -> llvm::Type* {
            llvm::Type* t = nullptr;
            if (m->getTypeByName(str))
                t = m->getTypeByName(str);
            else {
                auto int8ty = llvm::IntegerType::getInt8Ty(m->getContext());
                t = llvm::StructType::create(str, int8ty, nullptr);
            }
            g->AddEliminateType(t);
            return t;
        };
    } else {
        auto ty = nonstatic->Decay()->GetLLVMType(a);
        return [=](llvm::Module* m) -> llvm::Type* {
            llvm::Type* t = nullptr;
            if (m->getTypeByName(str))
                t = m->getTypeByName(str);
            else {
                t = llvm::StructType::create(str, ty(m)->getPointerTo(), nullptr);
            }
            return t;
        };
    }
}

std::size_t ClangOverloadSet::size(Analyzer& a) {
    if (!nonstatic) a.gen->GetInt8AllocSize();
    return a.gen->GetDataLayout().getPointerSize();
}
std::size_t ClangOverloadSet::alignment(Analyzer& a) {
    if (!nonstatic) return llvm::DataLayout(a.gen->GetDataLayout()).getABIIntegerTypeAlignment(8);
    return a.gen->GetDataLayout().getPointerABIAlignment();
}
ConcreteExpression ClangOverloadSet::BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("Cannot construct an overload set from more than one argument.");
    if (args.size() == 1) {
        if (args[0].t->IsReference(this))
            return args[0].t->BuildValue(args[0], a);
        if (args[0].t == this)
            return args[0];
        if (nonstatic) {
            if (args[0].t->IsReference(nonstatic) || (nonstatic->IsReference() && (nonstatic->IsReference() == args[0].t->IsReference()))) {
                auto var = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
                auto store = a.gen->CreateStore(a.gen->CreateFieldExpression(var, 0), args[0].Expr);
                return ConcreteExpression(this, a.gen->CreateChainExpression(store, a.gen->CreateLoad(var)));
            } 
            if (args[0].t == nonstatic) {
                assert("Internal compiler error: Attempt to call a member function of a value.");
            }
        }
        throw std::runtime_error("Can only construct overload set from another overload set of the same type, or a reference to T.");
    }
    if (nonstatic)
        throw std::runtime_error("Cannot default-construct a non-static overload set.");
    ConcreteExpression out;
    out.t = this;
    out.Expr = a.gen->CreateNull(GetLLVMType(a));
    return out;
}