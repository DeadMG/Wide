#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::Type* Reference::GetLLVMType(llvm::Module* module) {
    return Decay()->GetLLVMType(module)->getPointerTo();
}
Wide::Util::optional<clang::QualType> LvalueType::GetClangType(ClangTU& tu) {
    auto ty = Decay()->GetClangType(tu);
    if (!ty)
        return Wide::Util::none;
    return tu.GetASTContext().getLValueReferenceType(*ty);
}
Wide::Util::optional<clang::QualType> RvalueType::GetClangType(ClangTU& tu) {
    auto ty = Decay()->GetClangType(tu);
    if (!ty)
        return Wide::Util::none;
    return tu.GetASTContext().getRValueReferenceType(*ty);
}

std::size_t Reference::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t Reference::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}

bool RvalueType::IsA(Type* self, Type* other, Lexer::Access access) {
    if (other == this)
        return true;
    if (other == analyzer.GetLvalueType(Decay()))
        return false;
    // T&& is-a U&& if T* is-a U*
    if (IsRvalueType(other) && analyzer.GetPointerType(Decay())->IsA(analyzer.GetPointerType(Decay()), analyzer.GetPointerType(other->Decay()), access))
        return true;
    return Decay()->IsA(self, other, access);
}
bool LvalueType::IsA(Type* self, Type* other, Lexer::Access access) {
    if (other == this)
        return true;
    if (other == analyzer.GetRvalueType(Decay()))
        return false;
    // T& is-a U& if T* is-a U*
    if (IsLvalueType(other) && analyzer.GetPointerType(Decay())->IsA(analyzer.GetPointerType(Decay()), analyzer.GetPointerType(other->Decay()), access))
        return true;
    return Decay()->IsA(self, other, access);
}
struct rvalueconvertible : OverloadResolvable, Callable {
    rvalueconvertible(RvalueType* s)
    : self(s) {}
    RvalueType* self;
    Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
    std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
        if (types.size() != 2) return Util::none;
        if (types[0] != a.GetLvalueType(self)) return Util::none;
        if (IsLvalueType(types[1])) return Util::none;
        if (types[1] == self) return types;
        //if (types[1]->Decay() == self->Decay()) return Util::none;
        // If it is or pointer-is then yay, else nay.
        auto ptrt = a.GetPointerType(types[1]->Decay());
        auto ptrself = a.GetPointerType(self->Decay());
        if (!ptrt->IsA(ptrt, ptrself, GetAccessSpecifier(source, ptrt)) && !types[1]->Decay()->IsA(types[1]->Decay(), self->Decay(), GetAccessSpecifier(source, types[1]))) return Util::none;
        return types;
    }
    std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
        if (args[1]->GetType() == self)
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
        if (args[1]->GetType()->Decay() == self->Decay())
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]) ,Wide::Memory::MakeUnique<RvalueCast>(std::move(args[1])));
        // If pointer-is then use that, else go with value-is.
        auto ptrt = self->analyzer.GetPointerType(args[1]->GetType()->Decay());
        auto ptrself = self->analyzer.GetPointerType(self->Decay());
        if (ptrt->IsA(ptrt, ptrself, GetAccessSpecifier(c.from, ptrt))) {
            auto basety = args[1]->GetType()->Decay();
            if (!args[1]->GetType()->IsReference())
                args[1] = Wide::Memory::MakeUnique<RvalueCast>(std::move(args[1]));
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), basety->AccessBase(std::move(args[1]), self));
        }
        return self->Decay()->BuildRvalueConstruction(Expressions( std::move(args[1]) ), c);
    }
};
struct PointerComparableResolvable : OverloadResolvable, Callable {
    PointerComparableResolvable(Reference* s)
    : self(s) {}
    Reference* self;
    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
        if (types.size() != 2) return Util::none;
        if (types[0] != a.GetLvalueType(self)) return Util::none;
        if (IsRvalueType(types[1])) return Util::none;
        if (types[1] == self) return types;
        auto ptrt = a.GetPointerType(types[1]->Decay());
        auto ptrself = a.GetPointerType(self->Decay());
        if (!ptrt->IsA(ptrt, ptrself, GetAccessSpecifier(source, ptrt))) return Util::none;
        return types;
    }
    std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
    std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
        auto ty = args[1]->GetType();
        if (args[1]->GetType() == self)
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
        auto udt = ty->Decay();
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), udt->AccessBase(std::move(args[1]), self->Decay()));
    }
    Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
};
OverloadSet* RvalueType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    RvalueConvertible = Wide::Memory::MakeUnique<rvalueconvertible>(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(RvalueConvertible.get());
    return analyzer.GetOverloadSet(set);
}
OverloadSet* LvalueType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);

    DerivedConstructor = Wide::Memory::MakeUnique<PointerComparableResolvable>(this);

    std::unordered_set<OverloadResolvable*> set;
    set.insert(DerivedConstructor.get());
    return analyzer.GetOverloadSet(set);
}
std::string LvalueType::explain() {
    return Decay()->explain() + ".lvalue";
}
std::string RvalueType::explain() {
    return Decay()->explain() + ".rvalue";
}