#include <Wide/Semantic/ArrayType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Module.h>
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
std::shared_ptr<Expression> ArrayType::PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) {
    assert(num < count);
    return CreateResultExpression([=](Expression::InstanceKey f) {
        auto result_ty = IsLvalueType(self->GetType(f))
            ? t->analyzer.GetLvalueType(t)
            : IsRvalueType(self->GetType(f))
            ? t->analyzer.GetRvalueType(t)
            : t;
        return CreatePrimGlobal(result_ty, [=](CodegenContext& con) {
            auto val = self->GetValue(con);
            return con.CreateStructGEP(val, num);
        });
    });
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
OverloadSet* ArrayType::CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) {
    if (what != Parse::OperatorName({ &Lexer::TokenTypes::OpenSquareBracket, &Lexer::TokenTypes::CloseSquareBracket }))
        return AggregateType::CreateOperatorOverloadSet(what, access, kind);
    if (access != Parse::Access::Public)
        return AccessMember(what, Parse::Access::Public, kind);

    struct IndexOperatorResolvable : OverloadResolvable, Callable {
        IndexOperatorResolvable(ArrayType* el) : array(el) {}
        ArrayType* array;
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            args[1] = BuildValue(std::move(args[1]));
            auto globmod = c.from->analyzer.GetGlobalModule()->BuildValueConstruction({}, c);
            auto stdmod = Type::AccessMember(globmod, "Standard", c);
            if (!stdmod) throw std::runtime_error("No Standard module found!");
            auto errmod = Type::AccessMember(stdmod, "Error", c);
            if (!errmod) throw std::runtime_error("Standard.Error module not found!");
            auto array_bounds_exception = Type::AccessMember(errmod, "ArrayIndexOutOfBounds", c);
            if (!array_bounds_exception) throw std::runtime_error("Could not find Standard.Error.ArrayIndexOutOfBounds.");
            auto throw_ = Semantic::ThrowObject(Type::BuildCall(array_bounds_exception, {}, c), c);
            auto intty = dynamic_cast<IntegralType*>(args[1]->GetType(key));
            auto argty = args[0]->GetType(key);
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), Semantic::CollapseType(argty, array->t), [this, argty, throw_, intty](llvm::Value* arr, llvm::Value* index, CodegenContext& con) -> llvm::Value* {
                auto zero = (llvm::Value*)llvm::ConstantInt::get(intty->GetLLVMType(con), llvm::APInt(intty->GetLLVMType(con)->getBitWidth(), uint64_t(0), false));
                llvm::BasicBlock* out_of_bounds = llvm::BasicBlock::Create(con, "out_of_bounds", con->GetInsertBlock()->getParent());
                llvm::BasicBlock* in_bounds = llvm::BasicBlock::Create(con, "in_bounds", con->GetInsertBlock()->getParent());
                // if (index >= 0 && index < size)
                llvm::Value* is_in_bounds;
                if (intty->IsSigned()) {
                    auto is_gte_zero = con->CreateICmpSGE(index, zero);
                    auto is_lt_bound = con->CreateICmpSLT(index, llvm::ConstantInt::get(intty->GetLLVMType(con), llvm::APInt(intty->GetLLVMType(con)->getBitWidth(), uint64_t(array->count), false)));
                    is_in_bounds = con->CreateAnd(is_gte_zero, is_lt_bound);
                } else {
                    if (array->count > (std::pow(2, intty->GetBitness() - 1))) {
                        is_in_bounds = llvm::ConstantInt::getTrue(con);
                    } else {
                        is_in_bounds = con->CreateICmpULT(index, llvm::ConstantInt::get(intty->GetLLVMType(con), llvm::APInt(intty->GetLLVMType(con)->getBitWidth(), uint64_t(array->count), false)));
                    }
                }
                con->CreateCondBr(is_in_bounds, in_bounds, out_of_bounds);
                con->SetInsertPoint(out_of_bounds);
                throw_(con);
                con->SetInsertPoint(in_bounds);
                llvm::Value* indices[] = { zero, index };
                return Semantic::CollapseMember(argty, { con->CreateGEP(arr, indices), array->t }, con);
            });
        }
        Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
            if (args.size() != 2) return Util::none;
            auto arrty = dynamic_cast<ArrayType*>(args[0]->Decay());
            auto intty = dynamic_cast<IntegralType*>(args[1]->Decay());
            if (!arrty || !intty) return Util::none;
            return args;
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
        Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final { return this; }
    };
    if (!IndexOperator) IndexOperator = Wide::Memory::MakeUnique<IndexOperatorResolvable>(this);
    return analyzer.GetOverloadSet(IndexOperator.get());
}

OverloadSet* ArrayType::CreateConstructorOverloadSet(Parse::Access access) {
    return analyzer.GetOverloadSet(AggregateType::CreateConstructorOverloadSet(access), TupleInitializable::CreateConstructorOverloadSet(access));
}
bool ArrayType::AlwaysKeepInMemory(llvm::Module* mod) {
    return true;
}
std::shared_ptr<Expression> ArrayType::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) {
    if (name != "size") return nullptr;
    return std::make_shared<Integer>(llvm::APInt(64, (uint64_t)count, false), analyzer);
}