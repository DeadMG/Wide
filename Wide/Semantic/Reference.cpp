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
Wide::Util::optional<clang::QualType> LvalueType::GetClangType(ClangTU& tu, Analyzer& a) {
    auto ty = Decay()->GetClangType(tu, a);
    if (!ty)
        return Wide::Util::none;
    return tu.GetASTContext().getLValueReferenceType(*ty);
}
Wide::Util::optional<clang::QualType> RvalueType::GetClangType(ClangTU& tu, Analyzer& a) {
    auto ty = Decay()->GetClangType(tu, a);
    if (!ty)
        return Wide::Util::none;
    return tu.GetASTContext().getRValueReferenceType(*ty);
}

std::size_t Reference::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t Reference::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

bool RvalueType::IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) {
    if (other == this)
        return true;
    if (other == a.GetLvalueType(Decay()))
        return false;
    // T&& is-a U&& if T* is-a U*
    if (IsRvalueType(other) && a.GetPointerType(Decay())->IsA(a.GetPointerType(Decay()), a.GetPointerType(other->Decay()), a, access))
        return true;
    return Decay()->IsA(self, other, a, access);
}
bool LvalueType::IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) {
    if (other == this)
        return true;
    if (other == a.GetRvalueType(Decay()))
        return false;
    // T& is-a U& if T* is-a U*
    if (IsLvalueType(other) && a.GetPointerType(Decay())->IsA(a.GetPointerType(Decay()), a.GetPointerType(other->Decay()), a, access))
        return true;
    return Decay()->IsA(self, other, a, access);
}
struct PointerComparableResolvable : OverloadResolvable, Callable {
    PointerComparableResolvable(Reference* s)
    : self(s) {}
    Reference* self;
    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
        if (types.size() != 2) return Util::none;
        if (types[0] != a.GetLvalueType(self)) return Util::none;
        auto ptrt = a.GetPointerType(types[1]->Decay());
        auto ptrself = a.GetPointerType(self->Decay());
        if (!ptrt->IsA(ptrt, ptrself, a, GetAccessSpecifier(source, ptrt, a))) return Util::none;
        return types;
    }
    std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
    ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
        auto ptr = ConcreteExpression(c->GetPointerType(args[1].t->Decay()), args[1].Expr);
        return ConcreteExpression(args[0].t, c->GetPointerType(self->Decay())->BuildInplaceConstruction(args[0].Expr, { ptr }, c));
    }
    Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
};
OverloadSet* RvalueType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(a, Lexer::Access::Public);
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
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            if (types[0] != a.GetLvalueType(self)) return Util::none;
            if (IsLvalueType(types[1])) return Util::none;
            // If it is or pointer-is then yay, else nay.
            auto ptrt = a.GetPointerType(types[1]->Decay());
            auto ptrself = a.GetPointerType(self->Decay());
            if (!ptrt->IsA(ptrt, ptrself, a, GetAccessSpecifier(source, ptrt, a)) && !types[1]->Decay()->IsA(types[1]->Decay(), self->Decay(), a, GetAccessSpecifier(source, types[1], a))) return Util::none;
            return types;
        }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            // If pointer-is then use that, else go with value-is.
            auto ptrt = c->GetPointerType(args[1].t->Decay());
            auto ptrself = c->GetPointerType(self->Decay());
            if (ptrt->IsA(ptrt, ptrself, *c, GetAccessSpecifier(c, ptrt))) {
                auto ptr = ConcreteExpression(c->GetPointerType(args[1].t->Decay()), args[1].Expr);
                return ConcreteExpression(args[0].t, c->GetPointerType(self->Decay())->BuildInplaceConstruction(args[0].Expr, { ptr }, c));
            }
            return self->Decay()->BuildRvalueConstruction({ args[1] }, c);
        }
    };
    set.insert(a.arena.Allocate<rvalueconvertible>(this));
    return a.GetOverloadSet(set);
}
OverloadSet* LvalueType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(a, Lexer::Access::Public);
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
std::string LvalueType::explain(Analyzer& a) {
    return Decay()->explain(a) + ".lvalue";
}
std::string RvalueType::explain(Analyzer& a) {
    return Decay()->explain(a) + ".rvalue";
}