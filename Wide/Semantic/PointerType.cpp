#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<clang::QualType> PointerType::GetClangType(ClangTU& tu) {
    auto pointeety = pointee->GetClangType(tu);
    if (!pointeety) return Wide::Util::none;
    return tu.GetASTContext().getPointerType(*pointeety);
}

llvm::Type* PointerType::GetLLVMType(llvm::Module* module) {
    auto ty = pointee->GetLLVMType(module);
    if (ty->isVoidTy())
        ty = llvm::IntegerType::getInt8Ty(module->getContext());
    return llvm::PointerType::get(ty, 0);
}
std::size_t PointerType::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t PointerType::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}

PointerType::PointerType(Type* point, Analyzer& a)
: PrimitiveType(a) {
    pointee = point;
}

OverloadSet* PointerType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    struct PointerComparableResolvable : OverloadResolvable, Callable {
        PointerComparableResolvable(PointerType* s)
        : self(s) {}
        PointerType* self;
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            if (types[0] != a.GetLvalueType(self)) return Util::none;
            if (types[1]->Decay() == self) return Util::none;
            if (!dynamic_cast<PointerType*>(types[1]->Decay())) return Util::none;
            if (!types[1]->IsA(types[1], self, GetAccessSpecifier(source, types[1]))) return Util::none;
            return types;
        }
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            auto other = BuildValue(std::move(args[1]));
            auto udt = dynamic_cast<PointerType*>(other->GetType())->pointee;
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), udt->AccessBase(std::move(other), self->pointee));
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
    };
    auto usual = PrimitiveType::CreateConstructorOverloadSet(Lexer::Access::Public);
    NullConstructor = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
        return CreatePrimOp(std::move(args[0]), std::move(args[1]), analyzer.GetLvalueType(this), [this](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
            con->CreateStore(llvm::Constant::getNullValue(GetLLVMType(con)), lhs);
            return lhs;
        });
    }, { analyzer.GetLvalueType(this), analyzer.GetNullType() });
    DerivedConstructor = Wide::Memory::MakeUnique<PointerComparableResolvable>(this);
    // T* can be constructed from U* if U* is-a T*

    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(usual, analyzer.GetOverloadSet(NullConstructor.get())), analyzer.GetOverloadSet(DerivedConstructor.get())); 
}

std::unique_ptr<Expression> PointerType::BuildBooleanConversion(std::unique_ptr<Expression> c, Context) {
    return CreatePrimUnOp(BuildValue(std::move(c)), analyzer.GetBooleanType(), [](llvm::Value* v, CodegenContext& con) {
        return con->CreateZExt(con->CreateIsNotNull(v), llvm::Type::getInt8Ty(con));
    });
}

bool PointerType::IsA(Type* self, Type* other, Lexer::Access access) {
    // T* is U* if T is derived from U.
    // But reference to T* is not reference to U* so keep that shit under wraps yo.
    // T* or T*&& can be U* or U*&&
    // T*& can be U*
    if (Type::IsA(self, other, access)) return true;
    if (IsLvalueType(other)) return false;

    auto otherptr = dynamic_cast<PointerType*>(other->Decay());
    if (!otherptr) return false;
    return pointee->IsDerivedFrom(otherptr->pointee) == InheritanceRelationship::UnambiguouslyDerived;
}

OverloadSet* PointerType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access) {
    if (access != Lexer::Access::Public)
        return AccessMember(self, what, Lexer::Access::Public);
    if (what != &Lexer::TokenTypes::Star) return PrimitiveType::CreateOperatorOverloadSet(self, what, access);
    DereferenceOperator = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
        return CreatePrimUnOp(std::move(args[0]), analyzer.GetLvalueType(pointee), [](llvm::Value* val, CodegenContext& con) {
            return val;
        });
    }, { this });
    return analyzer.GetOverloadSet(DereferenceOperator.get());
}
std::string PointerType::explain() {
    return pointee->explain() + ".pointer";
}