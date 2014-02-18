#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Reference::GetLLVMType(Analyzer& a) {
    auto f = Decay()->GetLLVMType(a);
    return [=](llvm::Module* m) {
        return f(m)->getPointerTo();
    };
}
clang::QualType LvalueType::GetClangType(ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getLValueReferenceType(Decay()->GetClangType(tu, a));
}
clang::QualType RvalueType::GetClangType(ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getRValueReferenceType(Decay()->GetClangType(tu, a));
}

std::size_t Reference::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t Reference::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

bool RvalueType::IsA(Type* self, Type* other, Analyzer& a) {
    if (other == this)
        return true;
    if (other == a.GetLvalueType(Decay()))
        return false;
    // T&& is-a U&& if T* is-a U*
    if (IsRvalueType(other) && a.GetPointerType(Decay())->IsA(a.GetPointerType(Decay()), a.GetPointerType(other->Decay()), a))
        return true;
    return Decay()->IsA(self, other, a);
}
bool LvalueType::IsA(Type* self, Type* other, Analyzer& a) {
    if (other == this)
        return true;
    if (other == a.GetRvalueType(Decay()))
        return false;
    // T& is-a U& if T* is-a U*
    if (IsLvalueType(other) && a.GetPointerType(Decay())->IsA(a.GetPointerType(Decay()), a.GetPointerType(other->Decay()), a))
        return true;
    return Decay()->IsA(self, other, a);
}
struct PointerComparableResolvable : OverloadResolvable, Callable {
    PointerComparableResolvable(Reference* s)
    : self(s) {}
    Reference* self;
    unsigned GetArgumentCount() override final { return 2; }
    Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final {
        if (num == 0) {
            if (t == a.GetLvalueType(self))
                return t;
            else
                return nullptr;
        }
        auto ptrt = a.GetPointerType(t->Decay());
        auto ptrself = a.GetPointerType(self->Decay());
        if (ptrt->IsA(ptrt, ptrself, a))
            return t;
        return nullptr;
    }
    std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
    ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
        auto ptr = ConcreteExpression(c->GetPointerType(args[1].t->Decay()), args[1].Expr);
        return ConcreteExpression(args[0].t, c->GetPointerType(self->Decay())->BuildInplaceConstruction(args[0].Expr, { ptr }, c));
    }
    Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
};
OverloadSet* RvalueType::CreateConstructorOverloadSet(Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this)); 
    types.push_back(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
    //set.insert(a.arena.Allocate<PointerComparableResolvable>(this));
    struct rvalueconvertible : OverloadResolvable, Callable {
        rvalueconvertible(RvalueType* s)
        : self(s) {}
        RvalueType* self;
        unsigned GetArgumentCount() override final { return 2; }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final {
            if (num == 0) {
                if (t == a.GetLvalueType(self))
                    return t;
                return nullptr;
            }
            if (IsLvalueType(t)) return nullptr;
            // If it is or pointer-is then yay, else nay.
            auto ptrt = a.GetPointerType(t->Decay());
            auto ptrself = a.GetPointerType(self->Decay());
            if (ptrt->IsA(ptrt, ptrself, a))
                return t;

            if (t->Decay()->IsA(t->Decay(), self->Decay(), a))
                return t;
            return nullptr;
        }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            // If pointer-is then use that, else go with value-is.
            auto ptrt = c->GetPointerType(args[1].t->Decay());
            auto ptrself = c->GetPointerType(self->Decay());
            if (ptrt->IsA(ptrt, ptrself, *c)) {
                auto ptr = ConcreteExpression(c->GetPointerType(args[1].t->Decay()), args[1].Expr);
                return ConcreteExpression(args[0].t, c->GetPointerType(self->Decay())->BuildInplaceConstruction(args[0].Expr, { ptr }, c));
            }
            return self->Decay()->BuildRvalueConstruction({ args[1] }, c);
        }
    };
    set.insert(a.arena.Allocate<rvalueconvertible>(this));
    return a.GetOverloadSet(set);
}
OverloadSet* LvalueType::CreateConstructorOverloadSet(Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
    set.insert(a.arena.Allocate<PointerComparableResolvable>(this));
    return a.GetOverloadSet(set);
}