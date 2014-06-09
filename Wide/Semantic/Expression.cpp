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
llvm::Value* ImplicitLoadExpr::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return bb.CreateLoad(src->GetValue(module, bb, allocas));
}
void ImplicitLoadExpr::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return src->DestroyLocals(module, bb, allocas);
}

ImplicitStoreExpr::ImplicitStoreExpr(std::unique_ptr<Expression> memory, std::unique_ptr<Expression> value)
: mem(std::move(memory)), val(std::move(value)) {
    auto memty = mem->GetType();
    auto valty = val->GetType();
    assert(memty->IsReference(valty));
}
Type* ImplicitStoreExpr::GetType() {
    assert(mem->GetType()->IsReference(val->GetType()));
    return mem->GetType();
}
llvm::Value* ImplicitStoreExpr::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    auto memory = mem->GetValue(module, bb, allocas);
    auto value = val->GetValue(module, bb, allocas);
    bb.CreateStore(value, memory);
    return memory;
}
void ImplicitStoreExpr::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    val->DestroyLocals(module, bb, allocas);
    mem->DestroyLocals(module, bb, allocas);
}

ImplicitTemporaryExpr::ImplicitTemporaryExpr(Type* what, Context c)
: of(what), c(c)
{
    // Make sure this is prepared before code-generation time.
    of->BuildDestructorCall(Wide::Memory::MakeUnique<ExpressionReference>(this), c);
}
Type* ImplicitTemporaryExpr::GetType() {
    return of->analyzer.GetLvalueType(of);
}
llvm::Value* ImplicitTemporaryExpr::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    auto local = allocas.CreateAlloca(of->GetLLVMType(module));
    local->setAlignment(of->alignment());
    alloc = local;
    return alloc;
}
void ImplicitTemporaryExpr::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    of->BuildDestructorCall(Wide::Memory::MakeUnique<ExpressionReference>(this), c)->GetValue(module, bb, allocas);
}

LvalueCast::LvalueCast(std::unique_ptr<Expression> expr)
: expr(std::move(expr)) {}
Type* LvalueCast::GetType() {
    assert(IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetLvalueType(expr->GetType()->Decay());
}
llvm::Value* LvalueCast::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return expr->GetValue(module, bb, allocas);
}
void LvalueCast::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return expr->DestroyLocals(module, bb, allocas);
}

RvalueCast::RvalueCast(std::unique_ptr<Expression> ex)
: expr(std::move(ex)) {
    assert(!IsRvalueType(expr->GetType()));
}
Type* RvalueCast::GetType() {
    assert(!IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetRvalueType(expr->GetType()->Decay());
}
llvm::Value* RvalueCast::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    if (IsLvalueType(expr->GetType()))
        return expr->GetValue(module, bb, allocas);
    if (expr->GetType()->IsComplexType(module))
        return expr->GetValue(module, bb, allocas);
    assert(!IsRvalueType(expr->GetType()));
    auto tempalloc = allocas.CreateAlloca(expr->GetType()->GetLLVMType(module));
    tempalloc->setAlignment(expr->GetType()->alignment());
    bb.CreateStore(expr->GetValue(module, bb, allocas), tempalloc);
    return tempalloc;
}
void RvalueCast::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    expr->DestroyLocals(module, bb, allocas);
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
llvm::Value* ExpressionReference::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return expr->GetValue(module, bb, allocas);
}
void ExpressionReference::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {}
Expression* ExpressionReference::GetImplementation() {
    return expr->GetImplementation();
}

ImplicitAddressOf::ImplicitAddressOf(std::unique_ptr<Expression> expr, Context c)
: expr(std::move(expr)), c(c) { GetType(); }
Type* ImplicitAddressOf::GetType() {
    if (!IsLvalueType(expr->GetType())) throw AddressOfNonLvalue(expr->GetType(), c.where);
    return expr->GetType()->analyzer.GetPointerType(expr->GetType()->Decay());
}
llvm::Value* ImplicitAddressOf::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return expr->GetValue(module, bb, allocas);
}
void ImplicitAddressOf::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return expr->DestroyLocals(module, bb, allocas);
}

String::String(std::string str, Analyzer& a)
: str(std::move(str)), a(a){}
Type* String::GetType() {
    return a.GetTypeForString(str);
}
llvm::Value* String::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return bb.CreateGlobalStringPtr(str);
}
void String::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {}

Integer::Integer(llvm::APInt val, Analyzer& an)
: a(an), value(std::move(val)) {}
Type* Integer::GetType() {
    auto width = value.getBitWidth();
    if (width < 8)
        width = 8;
    width = std::pow(2, std::ceil(std::log2(width)));
    return a.GetIntegralType(width, true);
}
llvm::Value* Integer::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return llvm::ConstantInt::get(GetType()->GetLLVMType(module), value);
}
void Integer::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {}

Boolean::Boolean(bool b, Analyzer& a)
: b(b), a(a) {}
Type* Boolean::GetType() {
    return a.GetBooleanType();
}
llvm::Value* Boolean::ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    return bb.getInt8(b);
}
void Boolean::DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {}

std::unique_ptr<Expression> Semantic::CreatePrimUnOp(std::unique_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func) {
    struct PrimUnOp : Expression {
        PrimUnOp(std::unique_ptr<Expression> expr, Type* r, std::function<llvm::Value*(llvm::Value*, llvm::Module* module, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func)
        : src(std::move(expr)), ret(std::move(r)), action(std::move(func)) {}

        std::unique_ptr<Expression> src;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>&)> action;

        Type* GetType() override final {
            return ret;
        }
        llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            return action(src->GetValue(module, bb, allocas), module, bb, allocas);
        }
        void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            src->DestroyLocals(module, bb, allocas);
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<PrimUnOp>(std::move(self), ret, func);
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func) {
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(), func);
}
std::unique_ptr<Expression> Semantic::CreatePrimAssOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func) {
    auto ref = Wide::Memory::MakeUnique<ExpressionReference>(lhs.get());
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(ref), CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(lhs)), std::move(rhs), func));
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func) {
    struct PrimBinOp : Expression {
        PrimBinOp(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right, Type* t, std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> func)
        : lhs(std::move(left)), rhs(std::move(right)), ret(t), action(std::move(func)) {}
        std::unique_ptr<Expression> lhs, rhs;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> action;
        Type* GetType() override final { return ret; }
        void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            rhs->DestroyLocals(module, bb, allocas);
            lhs->DestroyLocals(module, bb, allocas);
        }
        llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            // Strict order of evaluation.
            auto left = lhs->GetValue(module, bb, allocas);
            auto right = rhs->GetValue(module, bb, allocas);
            return action(left, right, module, bb, allocas);
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
    return CreatePrimOp(std::move(lhs), std::move(rhs), rhs->GetType(), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& b, llvm::IRBuilder<>& allocas) {
        return rhs;
    });
}
llvm::Value* Expression::GetValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
    if (!val) val = ComputeValue(module, bb, allocas);
    auto selfty = GetType()->GetLLVMType(module);
    if (GetType()->IsComplexType(module)) {
        auto ptrty = llvm::dyn_cast<llvm::PointerType>(val->getType());
        assert(ptrty);
        assert(ptrty->getElementType() == selfty);
    } else {
        // Extra variable because VS debugger typically won't load Type or Expression functions.
        assert(val->getType() == selfty);
    }
    return val;
}
