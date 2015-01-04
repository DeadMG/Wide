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

bool RvalueType::IsSourceATarget(Type* source, Type* target, Type* context){
    if (source == analyzer.GetLvalueType(target->Decay()))
        return false;
    if (!IsLvalueType(source) && !IsLvalueType(target) && Type::IsFirstASecond(analyzer.GetPointerType(source->Decay()), analyzer.GetPointerType(target->Decay()), context))
        return true;

    // Our decayed type may have something more useful to say.
    return Decay()->IsSourceATarget(source, target, context);
}
bool LvalueType::IsSourceATarget(Type* source, Type* target, Type* context) {
    if (source == analyzer.GetRvalueType(target->Decay()))
        return false;
    if (IsLvalueType(source) && IsLvalueType(target) && Type::IsFirstASecond(analyzer.GetPointerType(source->Decay()), analyzer.GetPointerType(target->Decay()), context))
        return true;

    // Our decayed type may have something more useful to say.
    return Decay()->IsSourceATarget(source, target, context);
}
struct rvalueconvertible : OverloadResolvable, Callable {
    rvalueconvertible(RvalueType* s)
    : self(s) {}
    RvalueType* self;
    Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final { return this; }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
        if (types.size() != 2) return Util::none;
        if (types[0] != a.GetLvalueType(self)) return Util::none;
        if (IsLvalueType(types[1])) return Util::none;
        if (types[1] == self) return types;
        //if (types[1]->Decay() == self->Decay()) return Util::none;
        // If it is or pointer-is then yay, else nay.
        auto ptrt = a.GetPointerType(types[1]->Decay());
        auto ptrself = a.GetPointerType(self->Decay());
        if (!Type::IsFirstASecond(ptrt, ptrself, source) && !Type::IsFirstASecond(types[1]->Decay(), self->Decay(), source)) return Util::none;
        return types;
    }
    std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        if (args[1]->GetType(key) == self)
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
        if (args[1]->GetType(key)->Decay() == self->Decay())
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), Wide::Memory::MakeUnique<RvalueCast>(std::move(args[1])));
        // If pointer-is then use that, else go with value-is.
        auto ptrt = self->analyzer.GetPointerType(args[1]->GetType(key)->Decay());
        auto ptrself = self->analyzer.GetPointerType(self->Decay());
        if (Type::IsFirstASecond(ptrt, ptrself, c.from)) {
            auto args1 = args[1];
            auto basety = args1->GetType(key)->Decay();
            if (!args1->GetType(key)->IsReference())
                args1 = Wide::Memory::MakeUnique<RvalueCast>(std::move(args1));
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), Type::AccessBase(std::move(args1), self->Decay()));
        }
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), self->Decay()->BuildRvalueConstruction({ std::move(args[1]) }, c));
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
        if (!Type::IsFirstASecond(ptrt, ptrself, source)) return Util::none;
        return types;
    }
    std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
    std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        if (args[1]->GetType(key) == self)
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
        return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), Type::AccessBase(std::move(args[1]), self->Decay()));
    }
    Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final { return this; }
};
OverloadSet* RvalueType::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);
    RvalueConvertible = Wide::Memory::MakeUnique<rvalueconvertible>(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(RvalueConvertible.get());
    return analyzer.GetOverloadSet(set);
}
OverloadSet* LvalueType::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);

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
std::function<llvm::Constant*(llvm::Module*)> Reference::GetRTTI() {
    return Decay()->GetRTTI();
}
std::string LvalueType::Export() {
    return analyzer.GetTypeExport(Decay()) + ".lvalue";
}
std::string RvalueType::Export() {
    return analyzer.GetTypeExport(Decay()) + ".rvalue";
}