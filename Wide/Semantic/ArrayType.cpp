#include <Wide/Semantic/ArrayType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Expression.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

ArrayType::ArrayType(Analyzer& a, Type* t, unsigned num)
: AggregateType(a)
, t(t)
, count(num) {}

std::vector<Type*> ArrayType::GetMembers() {
    return std::vector<Type*>(count, t);
}
Wide::Util::optional<clang::QualType> ArrayType::GetClangType(ClangTU& TU) {
    auto root_ty = t->GetClangType(TU);
    if (!root_ty)
        return Wide::Util::none;
    return TU.GetASTContext().getConstantArrayType(*root_ty, llvm::APInt(32, (uint64_t)count, false), clang::ArrayType::ArraySizeModifier::Normal, 0);
}
Wide::Util::optional<std::vector<Type*>> ArrayType::GetTypesForTuple() {
    return GetMembers();
}
std::string ArrayType::explain() {
    return t->explain() + ".array(" + std::to_string(count) + ")";
}
std::unique_ptr<Expression> ArrayType::PrimitiveAccessMember(std::unique_ptr<Expression> self, unsigned num) {
    struct ArrayIndex : Expression {
        ArrayIndex(unsigned num, Type* elem, std::unique_ptr<Expression> self)
        : num(num), elem_ty(elem), self(std::move(self)) {}
        unsigned num;
        std::unique_ptr<Expression> self;
        Type* elem_ty;

        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto val = self->GetValue(con);
            if (!elem_ty->IsComplexType(con)) {
                if (val->getType()->isPointerTy())
                    return con->CreateStructGEP(val, num);
                return con->CreateExtractValue(val, num);
            }
            return con->CreateStructGEP(val, num);
        }
        Type* GetType() override final {
            if (IsLvalueType(self->GetType()))
                return elem_ty->analyzer.GetLvalueType(elem_ty);
            if (IsRvalueType(self->GetType()))
                return elem_ty->analyzer.GetRvalueType(elem_ty);
            return elem_ty;
        }
    };
    assert(num < count);
    return Wide::Memory::MakeUnique<ArrayIndex>(num, t, std::move(self));
}
std::size_t ArrayType::size() {
    return t->size() * count;
}
std::size_t ArrayType::alignment() {
    return t->alignment();
}
llvm::Type* ArrayType::GetLLVMType(llvm::Module* module) {
    return llvm::ArrayType::get(t->GetLLVMType(module), count);
}
OverloadSet* ArrayType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Lexer::Access access) {
    if (what != &Lexer::TokenTypes::OpenSquareBracket)
        return AggregateType::CreateOperatorOverloadSet(t, what, access);
    if (access != Lexer::Access::Public)
        return AccessMember(t, what, Lexer::Access::Public);

    struct IndexOperatorResolvable : OverloadResolvable, Callable {
        IndexOperatorResolvable(ArrayType* el) : array(el) {}
        ArrayType* array;
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) {
            args[1] = BuildValue(std::move(args[1]));
            auto get_zero = [](llvm::IntegerType* intty) {
                return llvm::ConstantInt::get(intty, llvm::APInt(intty->getBitWidth(), uint64_t(0), false));
            };
            auto ref_call = [get_zero](llvm::Value* arr, llvm::Value* index, CodegenContext& con) {
                auto intty = llvm::dyn_cast<llvm::IntegerType>(index->getType());
                llvm::Value* args[] = { get_zero(intty), index };
                return con->CreateGEP(arr, args);
            };
            if (IsLvalueType(args[0]->GetType()))
                return CreatePrimOp(std::move(args[0]), std::move(args[1]), array->t->analyzer.GetLvalueType(array->t), ref_call);
            if (IsRvalueType(args[0]->GetType()))
                return CreatePrimOp(std::move(args[0]), std::move(args[1]), array->t->analyzer.GetLvalueType(array->t), ref_call);
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), array->t, [this, get_zero](llvm::Value* arr, llvm::Value* index, CodegenContext& con) -> llvm::Value* {
                if (arr->getType()->isPointerTy()) {
                    // Happens if we are complex
                    auto intty = llvm::dyn_cast<llvm::IntegerType>(index->getType());
                    llvm::Value* args[] = { get_zero(intty), index };                    
                    return con->CreateGEP(arr, args);
                }
                auto alloc = con.alloca_builder->CreateAlloca(array->GetLLVMType(con));
                alloc->setAlignment(array->alignment());
                con->CreateStore(arr, alloc);
                auto intty = llvm::dyn_cast<llvm::IntegerType>(index->getType());
                llvm::Value* args[] = { get_zero(intty), index };
                return con->CreateLoad(con->CreateGEP(alloc, args));
            });
        }
        Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) {
            if (args.size() != 2) return Util::none;
            auto arrty = dynamic_cast<ArrayType*>(args[0]->Decay());
            auto intty = dynamic_cast<IntegralType*>(args[1]->Decay());
            if (!arrty || !intty) return Util::none;
            return args;
        }
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) { return args; }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) { return this; }
    };
    if (!IndexOperator) IndexOperator = Wide::Memory::MakeUnique<IndexOperatorResolvable>(this);
    return analyzer.GetOverloadSet(IndexOperator.get());
}

OverloadSet* ArrayType::CreateConstructorOverloadSet(Lexer::Access access) {
    return analyzer.GetOverloadSet(AggregateType::CreateConstructorOverloadSet(access), TupleInitializable::CreateConstructorOverloadSet(access));
}