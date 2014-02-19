#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

clang::QualType PointerType::GetClangType(ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getPointerType(pointee->GetClangType(tu, a));
}

std::function<llvm::Type*(llvm::Module*)> PointerType::GetLLVMType(Analyzer& a) {
    return [=, &a](llvm::Module* mod) {
        auto ty = pointee->GetLLVMType(a)(mod);
        if (ty->isVoidTy())
            ty = llvm::IntegerType::getInt8Ty(mod->getContext());
        return llvm::PointerType::get(ty, 0);
    };
}
std::size_t PointerType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t PointerType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

PointerType::PointerType(Type* point) {
    pointee = point;
}

OverloadSet* PointerType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(a, Lexer::Access::Public);
    struct PointerComparableResolvable : OverloadResolvable, Callable {
        PointerComparableResolvable(PointerType* s)
        : self(s) {}
        PointerType* self;
        unsigned GetArgumentCount() override final { return 2; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a, Type* source) override final {
            if (num == 0) {
                if (t == a.GetLvalueType(self))
                    return t;
                else
                    return nullptr;
            }
            if (t->Decay() == self) return nullptr;
            if (!dynamic_cast<PointerType*>(t->Decay())) return nullptr;
            if (t->IsA(t, self, a, GetAccessSpecifier(source, t, a)))
                return t;
            return nullptr;
        }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            auto other = args[1].BuildValue(c);
            auto udt = dynamic_cast<BaseType*>(dynamic_cast<PointerType*>(other.t)->pointee);
            return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, udt->AccessBase(self->pointee, other.Expr, *c)));
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
    };
    auto usual = PrimitiveType::CreateConstructorOverloadSet(a, Lexer::Access::Public);
    //return usual;
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(a.GetNullType());
    auto null = make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateNull(GetLLVMType(*c))));
    }, types, a);
    auto derived_conversion = a.arena.Allocate<PointerComparableResolvable>(this);
    // T* can be constructed from U* if U* is-a T*

    return a.GetOverloadSet(a.GetOverloadSet(usual, a.GetOverloadSet(null)), a.GetOverloadSet(derived_conversion)); 
}

Codegen::Expression* PointerType::BuildBooleanConversion(ConcreteExpression obj, Context c) {
    return c->gen->CreateNegateExpression(c->gen->CreateIsNullExpression(obj.BuildValue(c).Expr));
}

bool PointerType::IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) {
    // T* is U* if T is derived from U.
    // But reference to T* is not reference to U* so keep that shit under wraps yo.
    // T* or T*&& can be U* or U*&&
    // T*& can be U*
    if (Type::IsA(self, other, a, access)) return true;
    if (IsLvalueType(other)) return false;

    auto otherptr = dynamic_cast<PointerType*>(other->Decay());
    if (!otherptr) return false;
    auto udt = dynamic_cast<BaseType*>(pointee);
    if (!udt) return false;
    return udt->IsDerivedFrom(otherptr->pointee, a) == InheritanceRelationship::UnambiguouslyDerived;
}

OverloadSet* PointerType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access, Analyzer& a) {
    if (access != Lexer::Access::Public)
        return AccessMember(self, what, Lexer::Access::Public, a);
    if (what != Lexer::TokenType::Dereference) return PrimitiveType::CreateOperatorOverloadSet(self, what, access, a);
    return a.GetOverloadSet(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(c->GetLvalueType(pointee), args[0].BuildValue(c).Expr);
    }, { this }, a));
}