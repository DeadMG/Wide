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

OverloadSet* PointerType::CreateConstructorOverloadSet(Parse::Access access) {
    if (access != Parse::Access::Public) return GetConstructorOverloadSet(Parse::Access::Public);
    struct PointerComparableResolvable : OverloadResolvable, Callable {
        PointerComparableResolvable(PointerType* s)
        : self(s) {}
        PointerType* self;
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            if (types[0] != a.GetLvalueType(self)) return Util::none;
            if (types[1]->Decay() == self) return Util::none;
            if (!dynamic_cast<PointerType*>(types[1]->Decay())) return Util::none;
            if (!Type::IsFirstASecond(types[1], self, source)) return Util::none;
            return types;
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
        std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            auto other = BuildValue(std::move(args[1]));
            auto udt = dynamic_cast<PointerType*>(other->GetType())->pointee;
            return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), Type::AccessBase(std::move(other), self->pointee));
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
    };
    auto usual = PrimitiveType::CreateConstructorOverloadSet(Parse::Access::Public);
    NullConstructor = MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) {
        return CreatePrimOp(std::move(args[0]), std::move(args[1]), analyzer.GetLvalueType(this), [this](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
            con->CreateStore(llvm::Constant::getNullValue(GetLLVMType(con)), lhs);
            return lhs;
        });
    }, { analyzer.GetLvalueType(this), analyzer.GetNullType() });
    DerivedConstructor = Wide::Memory::MakeUnique<PointerComparableResolvable>(this);
    // T* can be constructed from U* if U* is-a T*

    return analyzer.GetOverloadSet(analyzer.GetOverloadSet(usual, analyzer.GetOverloadSet(NullConstructor.get())), analyzer.GetOverloadSet(DerivedConstructor.get())); 
}

bool PointerType::IsSourceATarget(Type* source, Type* target, Type* context) {
    // All pointer conversions are value conversions so big fat nope if target is an lvalue.
    if (IsLvalueType(target)) return false;

    // We accept: all nulls.
    if (source->Decay() == analyzer.GetNullType()) return true;

    // T*& can only be U*, not U*&&
    if (IsLvalueType(source) && IsRvalueType(target) && source->Decay() != target->Decay()) return false;

    auto sourceptr = dynamic_cast<PointerType*>(source->Decay());
    if (!sourceptr) return false;
    auto targetptr = dynamic_cast<PointerType*>(target->Decay());
    if (!targetptr) return false;
    return sourceptr->pointee->IsDerivedFrom(targetptr->pointee) == InheritanceRelationship::UnambiguouslyDerived;
}

OverloadSet* PointerType::CreateOperatorOverloadSet(Parse::OperatorName name, Parse::Access access) {
    if (access != Parse::Access::Public) return AccessMember(name, Parse::Access::Public);
    if (name.size() != 1) return AccessMember(name, Parse::Access::Public);
    auto what = name.front();
    if (what == &Lexer::TokenTypes::Star) {
        DereferenceOperator = MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(std::move(args[0]), analyzer.GetLvalueType(pointee), [](llvm::Value* val, CodegenContext& con) {
                return val;
            });
        }, { this });
        return analyzer.GetOverloadSet(DereferenceOperator.get());
    } else if (what == &Lexer::TokenTypes::QuestionMark) {
        BooleanOperator = MakeResolvable([this](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(BuildValue(std::move(args[0])), analyzer.GetBooleanType(), [](llvm::Value* v, CodegenContext& con) {
                return con->CreateZExt(con->CreateIsNotNull(v), llvm::Type::getInt8Ty(con));
            });
        }, { this });
        return analyzer.GetOverloadSet(BooleanOperator.get());
    }
    return PrimitiveType::CreateOperatorOverloadSet(name, access);
}
std::string PointerType::explain() {
    return pointee->explain() + ".pointer";
}