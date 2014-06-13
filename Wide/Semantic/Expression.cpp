#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>

using namespace Wide;
using namespace Semantic;

ImplicitLoadExpr::ImplicitLoadExpr(std::unique_ptr<Expression> arg)
: src(std::move(arg)) {}
Type* ImplicitLoadExpr::GetType() {
    assert(src->GetType()->IsReference());
    return src->GetType()->Decay();
}
llvm::Value* ImplicitLoadExpr::ComputeValue(CodegenContext& con) {
    return con->CreateLoad(src->GetValue(con));
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
llvm::Value* ImplicitStoreExpr::ComputeValue(CodegenContext& con) {
    auto memory = mem->GetValue(con);
    auto value = val->GetValue(con);
    con->CreateStore(value, memory);
    return memory;
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
llvm::Value* ImplicitTemporaryExpr::ComputeValue(CodegenContext& con) {
    auto local = con.alloca_builder->CreateAlloca(of->GetLLVMType(con));
    local->setAlignment(of->alignment());
    alloc = local;
    return alloc;
}
void ImplicitTemporaryExpr::DestroyExpressionLocals(CodegenContext& con) {
    of->BuildDestructorCall(Wide::Memory::MakeUnique<ExpressionReference>(this), c)->GetValue(con);
}

LvalueCast::LvalueCast(std::unique_ptr<Expression> expr)
: expr(std::move(expr)) {}
Type* LvalueCast::GetType() {
    assert(IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetLvalueType(expr->GetType()->Decay());
}
llvm::Value* LvalueCast::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

RvalueCast::RvalueCast(std::unique_ptr<Expression> ex)
: expr(std::move(ex)) {
    assert(!IsRvalueType(expr->GetType()));
}
Type* RvalueCast::GetType() {
    assert(!IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetRvalueType(expr->GetType()->Decay());
}
llvm::Value* RvalueCast::ComputeValue(CodegenContext& con) {
    if (IsLvalueType(expr->GetType()))
        return expr->GetValue(con);
    if (expr->GetType()->IsComplexType(con))
        return expr->GetValue(con);
    assert(!IsRvalueType(expr->GetType()));
    auto tempalloc = con.alloca_builder->CreateAlloca(expr->GetType()->GetLLVMType(con));
    tempalloc->setAlignment(expr->GetType()->alignment());
    con->CreateStore(expr->GetValue(con), tempalloc);
    return tempalloc;
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
llvm::Value* ExpressionReference::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}
Expression* ExpressionReference::GetImplementation() {
    return expr->GetImplementation();
}

ImplicitAddressOf::ImplicitAddressOf(std::unique_ptr<Expression> expr, Context c)
: expr(std::move(expr)), c(c) { GetType(); }
Type* ImplicitAddressOf::GetType() {
    if (!IsLvalueType(expr->GetType())) throw AddressOfNonLvalue(expr->GetType(), c.where);
    return expr->GetType()->analyzer.GetPointerType(expr->GetType()->Decay());
}
llvm::Value* ImplicitAddressOf::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

String::String(std::string str, Analyzer& a)
: str(std::move(str)), a(a){}
Type* String::GetType() {
    return a.GetTypeForString(str);
}
llvm::Value* String::ComputeValue(CodegenContext& con) {
    return con->CreateGlobalStringPtr(str);
}

Integer::Integer(llvm::APInt val, Analyzer& an)
: a(an), value(std::move(val)) {}
Type* Integer::GetType() {
    auto width = value.getBitWidth();
    if (width < 8)
        width = 8;
    width = std::pow(2, std::ceil(std::log2(width)));
    return a.GetIntegralType(width, true);
}
llvm::Value* Integer::ComputeValue(CodegenContext& con) {
    return llvm::ConstantInt::get(GetType()->GetLLVMType(con), value);
}

Boolean::Boolean(bool b, Analyzer& a)
: b(b), a(a) {}
Type* Boolean::GetType() {
    return a.GetBooleanType();
}
llvm::Value* Boolean::ComputeValue(CodegenContext& con) {
    return con->getInt8(b);
}

std::unique_ptr<Expression> Semantic::CreatePrimUnOp(std::unique_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> func) {
    struct PrimUnOp : Expression {
        PrimUnOp(std::unique_ptr<Expression> expr, Type* r, std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> func)
        : src(std::move(expr)), ret(std::move(r)), action(std::move(func)) {}

        std::unique_ptr<Expression> src;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> action;

        Type* GetType() override final {
            return ret;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            return action(src->GetValue(con), con);
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<PrimUnOp>(std::move(self), ret, func);
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(), func);
}
std::unique_ptr<Expression> Semantic::CreatePrimAssOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    auto ref = Wide::Memory::MakeUnique<ExpressionReference>(lhs.get());
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(ref), CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(lhs)), std::move(rhs), func));
}
std::unique_ptr<Expression> Semantic::CreatePrimOp(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    struct PrimBinOp : Expression {
        PrimBinOp(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right, Type* t, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func)
        : lhs(std::move(left)), rhs(std::move(right)), ret(t), action(std::move(func)) {}
        std::unique_ptr<Expression> lhs, rhs;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> action;
        Type* GetType() override final { return ret; }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            // Strict order of evaluation.
            auto left = lhs->GetValue(con);
            auto right = rhs->GetValue(con);
            return action(left, right, con);
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
    return CreatePrimOp(std::move(lhs), std::move(rhs), rhs->GetType(), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
        return rhs;
    });
}
llvm::Value* Expression::GetValue(CodegenContext& con) {
    if (!val) val = ComputeValue(con);
    auto selfty = GetType()->GetLLVMType(con);
    if (GetType()->IsComplexType(con)) {
        auto ptrty = llvm::dyn_cast<llvm::PointerType>(val->getType());
        assert(ptrty);
        assert(ptrty->getElementType() == selfty);
    } else {
        // Extra variable because VS debugger typically won't load Type or Expression functions.
        assert(val->getType() == selfty);
    }
    return val;
}

void CodegenContext::DestroyDifference(CodegenContext& other) {
    other.destructing = true;
    auto vec = GetAddedDestructors(other);
    for (auto rit = vec.rbegin(); rit != vec.rend(); ++rit)
        (*rit)->DestroyLocals(other);
    other.destructing = false;
}
void CodegenContext::DestroyAll(bool EH) {
    destructing = true;
    for (auto rit = Destructors.rbegin(); rit != Destructors.rend(); ++rit) {
        if (EH || ExceptionOnlyDestructors.find(*rit) == ExceptionOnlyDestructors.end())
            (*rit)->DestroyLocals(*this);
    }
    destructing = false;
}
llvm::PointerType* CodegenContext::GetInt8PtrTy() {
    return llvm::Type::getInt8PtrTy(*this);
}
llvm::Function* CodegenContext::GetEHPersonality() {
    auto val = module->getFunction("__gxx_personality_v0");
    if (!val) {
        // i32(...)*
        auto fty = llvm::FunctionType::get(llvm::Type::getInt8Ty(*this), true);
        val = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__gxx_personality_v0", module);
    }
    return val;
}
llvm::BasicBlock* CodegenContext::CreateLandingpadForEH() {
    assert(EHHandler);
    auto lpad = llvm::BasicBlock::Create(*this, "landingpad", insert_builder->GetInsertBlock()->getParent());
    auto sourceblock = insert_builder->GetInsertBlock();
    insert_builder->SetInsertPoint(lpad);
    llvm::Type* landingpad_ret_values[] = { GetInt8PtrTy(), llvm::IntegerType::getInt32Ty(*this) };
    auto pad = insert_builder->CreateLandingPad(llvm::StructType::get(*this, landingpad_ret_values, false), GetEHPersonality(), 1);
    for (auto rtti : EHHandler->types)
        pad->addClause(rtti);
    pad->addClause(llvm::Constant::getNullValue(GetInt8PtrTy()));
    // Nuke the local difference.
    // Then transfer control to the catch and add a phi for our incoming.
    EHHandler->context->DestroyDifference(*this);
    assert(!lpad->back().isTerminator());
    insert_builder->CreateBr(EHHandler->target);
    // Some destructors like short-circuit booleans do require more than one BB
    // so don't use the lpad block directly.
    EHHandler->phi->addIncoming(pad, insert_builder->GetInsertBlock());
    insert_builder->SetInsertPoint(sourceblock);
    return lpad;
}

llvm::BasicBlock* CodegenContext::GetUnreachableBlock() {
    auto source_block = insert_builder->GetInsertBlock();
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*this, "unreachable", source_block->getParent());
    insert_builder->SetInsertPoint(bb);
    insert_builder->CreateUnreachable();
    insert_builder->SetInsertPoint(source_block);
    return bb;
}

llvm::Type* CodegenContext::GetLpadType() {
    llvm::Type* landingpad_ret_values[] = { GetInt8PtrTy(), llvm::IntegerType::getInt32Ty(*this) };
    return llvm::StructType::get(*this, landingpad_ret_values, false);
}

