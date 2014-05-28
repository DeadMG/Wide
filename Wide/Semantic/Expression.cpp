#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/Analyzer.h>

using namespace Wide;
using namespace Semantic;

ImplicitLoadExpr::ImplicitLoadExpr(std::unique_ptr<Expression> arg)
: src(std::move(arg)) {}
Type* ImplicitLoadExpr::GetType() {
    assert(src->GetType()->IsReference());
    return src->GetType()->Decay();
}
llvm::Value* ImplicitLoadExpr::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return bb.CreateLoad(src->GetValue(g, bb));
}
void ImplicitLoadExpr::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return src->DestroyLocals(g, bb);
}

ImplicitStoreExpr::ImplicitStoreExpr(std::unique_ptr<Expression> memory, std::unique_ptr<Expression> value)
: mem(std::move(memory)), val(std::move(value)) {
    assert(mem->GetType()->IsReference(val->GetType()));
}
Type* ImplicitStoreExpr::GetType() {
    assert(mem->GetType()->IsReference(val->GetType()));
    return mem->GetType();
}
llvm::Value* ImplicitStoreExpr::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    auto memory = mem->GetValue(g, bb);
    auto value = val->GetValue(g, bb);
    bb.CreateStore(value, memory);
    return memory;
}
void ImplicitStoreExpr::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    val->DestroyLocals(g, bb);
    mem->DestroyLocals(g, bb);
}

ImplicitTemporaryExpr::ImplicitTemporaryExpr(Type* what, Context c)
: of(what), c(c)
{
}
Type* ImplicitTemporaryExpr::GetType() {
    return of->analyzer.GetLvalueType(of);
}
llvm::Value* ImplicitTemporaryExpr::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    auto local = bb.CreateAlloca(of->GetLLVMType(g));
    local->setAlignment(of->alignment());
    alloc = local;
    return alloc;
}
void ImplicitTemporaryExpr::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    of->BuildDestructorCall(Wide::Memory::MakeUnique<ExpressionReference>(this), c)->GetValue(g, bb);
}

LvalueCast::LvalueCast(std::unique_ptr<Expression> expr)
: expr(std::move(expr)) {}
Type* LvalueCast::GetType() {
    assert(IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetLvalueType(expr->GetType()->Decay());
}
llvm::Value* LvalueCast::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return expr->GetValue(g, bb);
}
void LvalueCast::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return expr->DestroyLocals(g, bb);
}

RvalueCast::RvalueCast(std::unique_ptr<Expression> ex)
: expr(std::move(ex)) {
    assert(!IsRvalueType(expr->GetType()));
}
Type* RvalueCast::GetType() {
    assert(!IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetRvalueType(expr->GetType()->Decay());
}
llvm::Value* RvalueCast::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    if (IsLvalueType(expr->GetType()))
        return expr->GetValue(g, bb);
    if (expr->GetType()->IsComplexType(g))
        return expr->GetValue(g, bb);
    assert(!IsRvalueType(expr->GetType()));
    auto tempalloc = bb.CreateAlloca(expr->GetType()->GetLLVMType(g));
    tempalloc->setAlignment(expr->GetType()->alignment());
    bb.CreateStore(expr->GetValue(g, bb), tempalloc);
    return tempalloc;
}
void RvalueCast::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    expr->DestroyLocals(g, bb);
}

ExpressionReference::ExpressionReference(Expression* e)
: expr(e) {
    ListenToNode(expr);
}
void ExpressionReference::OnNodeChanged(Node* n, Change what) {
    if (what == Change::Destroyed)
        assert(false);
    OnChange();
}
Type* ExpressionReference::GetType() {
    return expr->GetType();
}
llvm::Value* ExpressionReference::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return expr->GetValue(g, bb);
}
void ExpressionReference::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {}
Expression* ExpressionReference::GetImplementation() {
    return expr->GetImplementation();
}

ImplicitAddressOf::ImplicitAddressOf(std::unique_ptr<Expression> expr)
: expr(std::move(expr)) {}
Type* ImplicitAddressOf::GetType() {
    assert(IsLvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetPointerType(expr->GetType()->Decay());
}
llvm::Value* ImplicitAddressOf::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return expr->GetValue(g, bb);
}
void ImplicitAddressOf::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return expr->DestroyLocals(g, bb);
}

String::String(std::string str, Analyzer& a)
: str(std::move(str)), a(a){}
Type* String::GetType() {
    return a.GetTypeForString(str);
}
llvm::Value* String::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return bb.CreateGlobalStringPtr(str);
}
void String::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {}

Integer::Integer(llvm::APInt val, Analyzer& an)
: a(an), value(std::move(val)) {}
Type* Integer::GetType() {
    auto width = value.getBitWidth();
    if (width < 8)
        width = 8;
    width = std::pow(2, std::ceil(std::log2(width)));
    return a.GetIntegralType(width, true);
}
llvm::Value* Integer::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return llvm::ConstantInt::get(GetType()->GetLLVMType(g), value);
}
void Integer::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {}

Boolean::Boolean(bool b, Analyzer& a)
: b(b), a(a) {}
Type* Boolean::GetType() {
    return a.GetBooleanType();
}
llvm::Value* Boolean::ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    return bb.getInt8(b);
}
void Boolean::DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) {}

std::unique_ptr<Expression> Semantic::CreatePrimUnOp(std::unique_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> func) {
    struct PrimUnOp : Expression {
        PrimUnOp(std::unique_ptr<Expression> expr, Type* r, std::function<llvm::Value*(llvm::Value*, Codegen::Generator& g, llvm::IRBuilder<>&)> func)
        : src(std::move(expr)), ret(std::move(r)), action(std::move(func)) {}

        std::unique_ptr<Expression> src;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, Codegen::Generator& g, llvm::IRBuilder<>& bb)> action;

        Type* GetType() override final {
            return ret;
        }
        llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            return action(src->GetValue(g, bb), g, bb);
        }
        void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            src->DestroyLocals(g, bb);
        }
    };
    return Wide::Memory::MakeUnique<PrimUnOp>(std::move(self), ret, func);
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> func) {
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(), func);
}
std::unique_ptr<Expression> Semantic::CreatePrimAssOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> func) {
    auto ref = Wide::Memory::MakeUnique<ExpressionReference>(lhs.get());
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(ref), CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(lhs)), std::move(rhs), func));
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> func) {
    struct PrimBinOp : Expression {
        PrimBinOp(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right, Type* t, std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> func)
        : lhs(std::move(left)), rhs(std::move(right)), ret(t), action(std::move(func)) {}
        std::unique_ptr<Expression> lhs, rhs;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, llvm::Value*, Codegen::Generator&, llvm::IRBuilder<>&)> action;
        Type* GetType() override final { return ret; }
        void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            rhs->DestroyLocals(g, bb);
            lhs->DestroyLocals(g, bb);
        }
        llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            // Strict order of evaluation.
            auto left = lhs->GetValue(g, bb);
            auto right = rhs->GetValue(g, bb);
            return action(left, right, g, bb);
        }
    };
    return Wide::Memory::MakeUnique<PrimBinOp>(std::move(lhs), std::move(rhs), ret, std::move(func));
}
std::unique_ptr<Expression> Semantic::BuildValue(std::unique_ptr<Expression> e) {
    if (e->GetType()->IsReference())
        return Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(e));
    return std::move(e);
}
std::unique_ptr<Expression> Semantic::BuildChain(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs) {
    assert(lhs);
    assert(rhs);
    return CreatePrimOp(std::move(lhs), std::move(rhs), rhs->GetType(), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& b) {
        return rhs;
    });
}
llvm::Value* Expression::GetValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) {
    if (!val) val = ComputeValue(g, bb);
    auto selfty = GetType()->GetLLVMType(g);
    if (GetType()->IsComplexType(g)) {
        auto ptrty = llvm::dyn_cast<llvm::PointerType>(val->getType());
        assert(ptrty);
        assert(ptrty->getElementType() == selfty);
    } else {
        // Extra variable because VS debugger typically won't load Type or Expression functions.
        assert(val->getType() == selfty);
    }
    return val;
}
