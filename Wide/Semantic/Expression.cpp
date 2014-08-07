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

ImplicitLoadExpr::ImplicitLoadExpr(std::shared_ptr<Expression> arg)
: src(std::move(arg)) {}
Type* ImplicitLoadExpr::GetType() {
    assert(src->GetType()->IsReference());
    return src->GetType()->Decay();
}
llvm::Value* ImplicitLoadExpr::ComputeValue(CodegenContext& con) {
    return con->CreateLoad(src->GetValue(con));
}

ImplicitStoreExpr::ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value)
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
}
Type* ImplicitTemporaryExpr::GetType() {
    return of->analyzer.GetLvalueType(of);
}
llvm::Value* ImplicitTemporaryExpr::ComputeValue(CodegenContext& con) {
    auto local = con.CreateAlloca(of);
    alloc = local;
    return alloc;
}

LvalueCast::LvalueCast(std::shared_ptr<Expression> expr)
: expr(std::move(expr)) {}
Type* LvalueCast::GetType() {
    assert(IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetLvalueType(expr->GetType()->Decay());
}
llvm::Value* LvalueCast::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

RvalueCast::RvalueCast(std::shared_ptr<Expression> ex)
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
    if (expr->GetType()->AlwaysKeepInMemory())
        return expr->GetValue(con);
    assert(!IsRvalueType(expr->GetType()));
    auto tempalloc = con.CreateAlloca(expr->GetType());
    con->CreateStore(expr->GetValue(con), tempalloc);
    return tempalloc;
}

ImplicitAddressOf::ImplicitAddressOf(std::shared_ptr<Expression> expr, Context c)
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

std::shared_ptr<Expression> Semantic::CreatePrimUnOp(std::shared_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> func) {
    struct PrimUnOp : Expression {
        PrimUnOp(std::shared_ptr<Expression> expr, Type* r, std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> func)
        : src(std::move(expr)), ret(std::move(r)), action(std::move(func)) {}

        std::shared_ptr<Expression> src;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, CodegenContext& con)> action;

        Type* GetType() override final {
            return ret;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto val = action(src->GetValue(con), con);
            if (ret != ret->analyzer.GetVoidType())
                assert(val);
            return val;
        }
    };
    assert(self);
    return Wide::Memory::MakeUnique<PrimUnOp>(std::move(self), ret, func);
}
std::shared_ptr<Expression> Semantic::CreatePrimGlobal(Type* ret, std::function<llvm::Value*(CodegenContext& con)> func) {
    struct PrimGlobalOp : Expression {
        PrimGlobalOp(Type* r, std::function<llvm::Value*(CodegenContext& con)> func)
        : ret(std::move(r)), action(std::move(func)) {}

        Type* ret;
        std::function<llvm::Value*(CodegenContext& con)> action;

        Type* GetType() override final {
            return ret;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto val = action(con);
            if (ret != ret->analyzer.GetVoidType())
                assert(val);
            return val;
        }
    };
    return Wide::Memory::MakeUnique<PrimGlobalOp>(ret, func);
}
std::shared_ptr<Expression> Semantic::CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(), func);
}
std::shared_ptr<Expression> Semantic::CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(lhs, CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(lhs), std::move(rhs), func));
}
std::shared_ptr<Expression> Semantic::CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    struct PrimBinOp : Expression {
        PrimBinOp(std::shared_ptr<Expression> left, std::shared_ptr<Expression> right, Type* t, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func)
        : lhs(std::move(left)), rhs(std::move(right)), ret(t), action(std::move(func)) {}
        std::shared_ptr<Expression> lhs, rhs;
        Type* ret;
        std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> action;
        Type* GetType() override final { return ret; }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            // Strict order of evaluation.
            auto left = lhs->GetValue(con);
            auto right = rhs->GetValue(con);
            auto val = action(left, right, con);
            if (ret != ret->analyzer.GetVoidType())
                assert(val);
            return val;
        }
    };
    return Wide::Memory::MakeUnique<PrimBinOp>(std::move(lhs), std::move(rhs), ret, std::move(func));
}
std::shared_ptr<Expression> Semantic::BuildValue(std::shared_ptr<Expression> e) {
    if (e->GetType()->IsReference())
        return Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(e));
    return std::move(e);
}
Chain::Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result)
: SideEffect(std::move(effect)), result(std::move(result)) {}
Type* Chain::GetType() {
    return result->GetType();
}
llvm::Value* Chain::ComputeValue(CodegenContext& con) {
    SideEffect->GetValue(con);
    return result->GetValue(con);
}
Expression* Chain::GetImplementation() {
    return result->GetImplementation();
}
std::shared_ptr<Expression> Semantic::BuildChain(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs) {
    assert(lhs);
    assert(rhs);
    return Wide::Memory::MakeUnique<Chain>(std::move(lhs), std::move(rhs));
}
llvm::Value* Expression::GetValue(CodegenContext& con) {
    if (val) return *val; 
    val = ComputeValue(con);
    auto selfty = GetType()->GetLLVMType(con);
    if (GetType()->AlwaysKeepInMemory()) {
        auto ptrty = llvm::dyn_cast<llvm::PointerType>((*val)->getType());
        assert(ptrty);
        assert(ptrty->getElementType() == selfty);
    } else if (selfty != llvm::Type::getVoidTy(con)) {
        // Extra variable because VS debugger typically won't load Type or Expression functions.
        assert((*val)->getType() == selfty);
    } else
        assert(!*val || (*val)->getType() == selfty);
    return *val;
}

void CodegenContext::DestroyDifference(CodegenContext& other, bool EH) {
    other.destructing = true;
    auto vec = GetAddedDestructors(other);
    for (auto rit = vec.rbegin(); rit != vec.rend(); ++rit)
        if (EH || !rit->second)
            rit->first(other);
    other.destructing = false;
}
void CodegenContext::DestroyAll(bool EH) {
    destructing = true;
    for (auto rit = Destructors.rbegin(); rit != Destructors.rend(); ++rit) 
        if (EH || !rit->second)
           rit->first(*this);
    destructing = false;
}
llvm::PointerType* CodegenContext::GetInt8PtrTy() {
    return llvm::Type::getInt8PtrTy(*this);
}
llvm::Function* CodegenContext::GetEHPersonality() {
    auto val = module->getFunction("__gxx_personality_v0");
    if (!val) {
        // i32(...)*
        auto fty = llvm::FunctionType::get(llvm::Type::getInt32Ty(*this), true);
        val = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__gxx_personality_v0", module);
    }
    return val;
}
llvm::BasicBlock* CodegenContext::CreateLandingpadForEH() {
    auto lpad = llvm::BasicBlock::Create(*this, "landingpad", insert_builder->GetInsertBlock()->getParent());
    auto sourceblock = insert_builder->GetInsertBlock();
    insert_builder->SetInsertPoint(lpad);
    llvm::Type* landingpad_ret_values[] = { GetInt8PtrTy(), llvm::IntegerType::getInt32Ty(*this) };
    auto pad = insert_builder->CreateLandingPad(llvm::StructType::get(*this, landingpad_ret_values, false), GetEHPersonality(), 1);
    if (!EHHandler) {
        pad->setCleanup(true);
        DestroyAll(true);
        insert_builder->CreateResume(pad);
        insert_builder->SetInsertPoint(sourceblock);
        return lpad;
    }
    for (auto rtti : EHHandler->types)
        pad->addClause(rtti);
    pad->addClause(llvm::Constant::getNullValue(GetInt8PtrTy()));
    // Nuke the local difference.
    // Then transfer control to the catch and add a phi for our incoming.
    EHHandler->context->DestroyDifference(*this, true);
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

llvm::Function* CodegenContext::GetCXABeginCatch() {
    auto val = module->getFunction("__cxa_begin_catch");
    if (!val) {
        // void *__cxa_begin_catch ( void *exceptionObject );
        auto fty = llvm::FunctionType::get(GetInt8PtrTy(), { GetInt8PtrTy() }, true);
        val = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_begin_catch", module);
    }
    return val;
}
llvm::Function* CodegenContext::GetCXAEndCatch() {
    auto val = module->getFunction("__cxa_end_catch");
    if (!val) {
        // void __cxa_end_catch ();
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(*this), {}, false);
        val = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_end_catch", module);
    }
    return val;
}
llvm::Function* CodegenContext::GetCXARethrow() {
    auto val = module->getFunction("__cxa_rethrow");
    if (!val) {
        // void __cxa_rethrow ();
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(*this), {}, false);
        val = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_rethrow", module);
    }
    return val;
}
void CodegenContext::GenerateCodeAndDestroyLocals(std::function<void(CodegenContext&)> action) {
    auto nested = *this;
    action(nested);
    if (!IsTerminated(insert_builder->GetInsertBlock()))
        DestroyDifference(nested, false);
}
bool CodegenContext::IsTerminated(llvm::BasicBlock* bb) {
    return !bb->empty() && bb->back().isTerminator();
}
bool CodegenContext::HasDestructors() {
    return !Destructors.empty();
}
std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator CodegenContext::AddDestructor(std::function<void(CodegenContext&)> func) {
    Destructors.push_back({ func, false });
    return --Destructors.end();
}
std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator CodegenContext::AddExceptionOnlyDestructor(std::function<void(CodegenContext&)> func) {
    Destructors.push_back({ func, true });
    return --Destructors.end();
}
void CodegenContext::EraseDestructor(std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator it) {
    Destructors.erase(it);
}
void CodegenContext::AddDestructors(std::list<std::pair<std::function<void(CodegenContext&)>, bool>> list) {
    Destructors.insert(Destructors.end(), list.begin(), list.end());
}
llvm::AllocaInst* CodegenContext::CreateAlloca(Type* t) {
    auto alloc = alloca_builder->CreateAlloca(t->GetLLVMType(module));
    alloc->setAlignment(t->alignment());
    return alloc;
}
llvm::Value* CodegenContext::CreateStructGEP(llvm::Value* v, unsigned num) {
    if (auto alloc = llvm::dyn_cast<llvm::AllocaInst>(v)) {
        if (gep_map->find(alloc) == gep_map->end()
         || gep_map->at(alloc).find(num) == gep_map->at(alloc).end())
          (*gep_map)[alloc][num] = gep_builder->CreateStructGEP(v, num);
        return (*gep_map)[alloc][num];
    }
    return insert_builder->CreateStructGEP(v, num);
}
void CodegenContext::EmitFunctionBody(llvm::Function* func, std::function<void(CodegenContext&)> body) {
    llvm::BasicBlock* allocas = llvm::BasicBlock::Create(func->getParent()->getContext(), "allocas", func);
    llvm::BasicBlock* geps = llvm::BasicBlock::Create(func->getParent()->getContext(), "geps", func);
    llvm::BasicBlock* entries = llvm::BasicBlock::Create(func->getParent()->getContext(), "entry", func);
    llvm::IRBuilder<> allocabuilder(allocas);
    allocabuilder.SetInsertPoint(allocabuilder.CreateBr(geps));
    llvm::IRBuilder<> gepbuilder(geps);
    gepbuilder.SetInsertPoint(gepbuilder.CreateBr(entries));
    llvm::IRBuilder<> insertbuilder(entries);
    CodegenContext newcon(func->getParent(), allocabuilder, gepbuilder, insertbuilder);
    body(newcon);
}
CodegenContext::CodegenContext(llvm::Module* mod, llvm::IRBuilder<>& alloc_builder, llvm::IRBuilder<>& gep_builder, llvm::IRBuilder<>& ir_builder)
    : module(mod), alloca_builder(&alloc_builder), gep_builder(&gep_builder), insert_builder(&ir_builder)
{
    gep_map = std::make_shared<std::unordered_map<llvm::AllocaInst*, std::unordered_map<unsigned, llvm::Value*>>>();
}
DestructorCall::DestructorCall(std::function<void(CodegenContext&)> destructor, Analyzer& a)
    : destructor(destructor), a(&a) {}
Type* DestructorCall::GetType()  {
    return a->GetVoidType();
}
llvm::Value* DestructorCall::ComputeValue(CodegenContext& con) {
    destructor(con);
    return nullptr;
}
llvm::Instruction* CodegenContext::GetAllocaInsertPoint() {
    return alloca_builder->GetInsertPoint();
}