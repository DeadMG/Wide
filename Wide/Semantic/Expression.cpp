#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionSkeleton.h>
#include <llvm/IR/Verifier.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>

using namespace Wide;
using namespace Semantic;

ImplicitLoadExpr::ImplicitLoadExpr(std::shared_ptr<Expression> arg)
    : SourceExpression({ arg }), src(std::move(arg)) {}
Type* ImplicitLoadExpr::CalculateType(InstanceKey f) {
    assert(src->GetType(f)->IsReference());
    return src->GetType(f)->Decay();
}
llvm::Value* ImplicitLoadExpr::ComputeValue(CodegenContext& con) {
    return con->CreateLoad(src->GetValue(con));
}

ImplicitStoreExpr::ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value)
    : SourceExpression({ memory, value }), mem(std::move(memory)), val(std::move(value)) {}
Type* ImplicitStoreExpr::CalculateType(InstanceKey f) {
    assert(mem->GetType(f)->IsReference(val->GetType(f)));
    return mem->GetType(f);
}
llvm::Value* ImplicitStoreExpr::ComputeValue(CodegenContext& con) {
    auto memory = mem->GetValue(con);
    auto value = val->GetValue(con);
    con->CreateStore(value, memory);
    return memory;
}
void Expression::Instantiate(Function* f) {
    GetType(f->GetSignature()->GetArguments());
}

ImplicitTemporaryExpr::ImplicitTemporaryExpr(Type* what, Context c)
: of(what), c(c)
{
}
Type* ImplicitTemporaryExpr::GetType(InstanceKey) {
    return of->analyzer.GetLvalueType(of);
}
llvm::Value* ImplicitTemporaryExpr::ComputeValue(CodegenContext& con) {
    auto local = con.CreateAlloca(of);
    alloc = local;
    return alloc;
}

LvalueCast::LvalueCast(std::shared_ptr<Expression> expr)
    : SourceExpression({ expr }), expr(std::move(expr)) {}
Type* LvalueCast::CalculateType(InstanceKey f) {
    assert(IsRvalueType(expr->GetType(f)));
    return expr->GetType(f)->analyzer.GetLvalueType(expr->GetType(f)->Decay());
}
llvm::Value* LvalueCast::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

RvalueCast::RvalueCast(std::shared_ptr<Expression> ex)
    : SourceExpression({ ex }), expr(std::move(ex)) {}
Type* RvalueCast::CalculateType(InstanceKey f) {
    assert(!IsRvalueType(expr->GetType(f)));
    return expr->GetType(f)->analyzer.GetRvalueType(expr->GetType(f)->Decay());
}
llvm::Value* RvalueCast::ComputeValue(CodegenContext& con) {
    if (IsLvalueType(expr->GetType(con.func)))
        return expr->GetValue(con);
    if (expr->GetType(con.func)->AlwaysKeepInMemory(con))
        return expr->GetValue(con);
    assert(!IsRvalueType(expr->GetType(con.func)));
    auto tempalloc = con.CreateAlloca(expr->GetType(con.func));
    con->CreateStore(expr->GetValue(con), tempalloc);
    return tempalloc;
}

ImplicitAddressOf::ImplicitAddressOf(std::shared_ptr<Expression> expr, Context c)
    : SourceExpression({ expr }), expr(std::move(expr)), c(c) {}
Type* ImplicitAddressOf::CalculateType(InstanceKey f) {
    auto ty = expr->GetType(f);
    if (!IsLvalueType(ty)) throw AddressOfNonLvalue(ty, c.where);
    return expr->GetType(f)->analyzer.GetPointerType(ty->Decay());
}
llvm::Value* ImplicitAddressOf::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

String::String(std::string str, Analyzer& a)
: str(std::move(str)), a(a){}
Type* String::GetType(InstanceKey f) {
    return a.GetLiteralStringType();
}
llvm::Value* String::ComputeValue(CodegenContext& con) {
    return con->CreateGlobalStringPtr(str);
}

Integer::Integer(llvm::APInt val, Analyzer& an)
: a(an), value(std::move(val)) {}
Type* Integer::GetType(InstanceKey f) {
    auto width = value.getBitWidth();
    if (width < 8)
        width = 8;
    width = std::pow(2, std::ceil(std::log2(width)));
    return a.GetIntegralType(width, true);
}
llvm::Value* Integer::ComputeValue(CodegenContext& con) {
    return llvm::ConstantInt::get(GetType(con.func)->GetLLVMType(con), value);
}

Boolean::Boolean(bool b, Analyzer& a)
: b(b), a(a) {}
Type* Boolean::GetType(InstanceKey f) {
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

        Type* GetType(InstanceKey f) override final {
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

        Type* GetType(InstanceKey f) override final {
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
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(nullptr), func);
}
std::shared_ptr<Expression> Semantic::CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(lhs, CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(lhs), std::move(rhs), func));
}
std::shared_ptr<Expression> Semantic::CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return CreatePrimGlobal(ret, [=](CodegenContext& con) {
        auto left = lhs->GetValue(con);
        auto right = rhs->GetValue(con);
        auto val = func(left, right, con);
        if (ret != ret->analyzer.GetVoidType())
            assert(val);
        return val;
    });
}
std::shared_ptr<Expression> Semantic::BuildValue(std::shared_ptr<Expression> e) {
    if (e->GetType(nullptr)->IsReference())
        return Wide::Memory::MakeUnique<ImplicitLoadExpr>(std::move(e));
    return std::move(e);
}
Chain::Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result)
    : SourceExpression({ result }), SideEffect(std::move(effect)), result(std::move(result)) {}
Type* Chain::CalculateType(InstanceKey f) {
    return result->GetType(f);
}
llvm::Value* Chain::ComputeValue(CodegenContext& con) {
    SideEffect->GetValue(con);
    return result->GetValue(con);
}
bool Chain::IsConstantExpression(InstanceKey f) {
    return result->IsConstantExpression(f) && SideEffect->IsConstantExpression(f);
}
std::shared_ptr<Expression> Semantic::BuildChain(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs) {
    assert(lhs);
    assert(rhs);
    return Wide::Memory::MakeUnique<Chain>(std::move(lhs), std::move(rhs));
}
llvm::Value* Expression::GetValue(CodegenContext& con) {
    auto func = con->GetInsertBlock()->getParent();
    if (values.find(func) != values.end())
        return values[func];
    llvm::Value*& val = values[func];
    val = ComputeValue(con);
    auto selfty = GetType(con.func)->GetLLVMType(con);
    if (GetType(con.func)->AlwaysKeepInMemory(con)) {
        auto ptrty = llvm::dyn_cast<llvm::PointerType>(val->getType());
        assert(ptrty);
        assert(ptrty->getElementType() == selfty);
    } else if (selfty != llvm::Type::getVoidTy(con)) {
        // Extra variable because VS debugger typically won't load Type or Expression functions.
        assert(val->getType() == selfty);
    } else
        assert(!val || val->getType() == selfty);
    return val;
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
    if (llvm::verifyFunction(*func))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
}
CodegenContext::CodegenContext(llvm::Module* mod, llvm::IRBuilder<>& alloc_builder, llvm::IRBuilder<>& gep_builder, llvm::IRBuilder<>& ir_builder)
    : module(mod), alloca_builder(&alloc_builder), gep_builder(&gep_builder), insert_builder(&ir_builder)
{
    gep_map = std::make_shared<std::unordered_map<llvm::AllocaInst*, std::unordered_map<unsigned, llvm::Value*>>>();
}
DestructorCall::DestructorCall(std::function<void(CodegenContext&)> destructor, Analyzer& a)
    : destructor(destructor), a(&a) {}
Type* DestructorCall::GetType(InstanceKey f)  {
    return a->GetVoidType();
}
llvm::Value* DestructorCall::ComputeValue(CodegenContext& con) {
    destructor(con);
    return nullptr;
}

llvm::Instruction* CodegenContext::GetAllocaInsertPoint() {
    return alloca_builder->GetInsertPoint();
}

llvm::Function* CodegenContext::GetCXAThrow() {
    auto cxa_throw = module->getFunction("__cxa_throw");
    if (!cxa_throw) {
        llvm::Type* args[] = { GetInt8PtrTy(), GetInt8PtrTy(), GetInt8PtrTy() };
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), args, false);
        cxa_throw = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_throw", module);
    }
    return cxa_throw;
}

llvm::Function* CodegenContext::GetCXAAllocateException() {
    auto allocate_exception = module->getFunction("__cxa_allocate_exception");
    if (!allocate_exception) {
        llvm::Type* args[] = { GetPointerSizedIntegerType() };
        auto fty = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(module->getContext()), args, false);
        allocate_exception = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_allocate_exception", module);
    }
    return allocate_exception;
}

llvm::Function* CodegenContext::GetCXAFreeException() {
    auto free_exception = module->getFunction("__cxa_free_exception");
    if (!free_exception) {
        llvm::Type* args[] = { GetInt8PtrTy() };
        auto fty = llvm::FunctionType::get(llvm::Type::getVoidTy(module->getContext()), args, false);
        free_exception = llvm::Function::Create(fty, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__cxa_free_exception", module);
    }
    return free_exception;
}

llvm::IntegerType* CodegenContext::GetPointerSizedIntegerType() {
    return llvm::IntegerType::get(module->getContext(), llvm::DataLayout(module->getDataLayout()->getStringRepresentation()).getPointerSizeInBits());
}
SourceExpression::SourceExpression(std::initializer_list<std::shared_ptr<Expression>> init_exprs) 
    : SourceExpression(init_exprs, {}) {}
SourceExpression::SourceExpression(std::initializer_list<std::shared_ptr<Expression>> init_exprs, const std::vector<std::shared_ptr<Expression>>& args) {
    auto lambda = [this](std::shared_ptr<Expression> expr) {
        exprs.insert(std::make_pair(expr.get(), ExpressionData{
            {},
            expr->OnChanged.connect([this](Expression* e, InstanceKey f) {
                auto newtype = e->GetType(f);
                if (exprs.at(e).types[f] != newtype) {
                    exprs.at(e).types[f] = newtype;
                    bool recalc = true;
                    for (auto&& arg : exprs) {
                        if (arg.second.types[f] == nullptr)
                            recalc = false;
                    }
                    if (recalc) {
                        auto our_currtype = curr_type[f];
                        curr_type[f] = CalculateType(f);
                        if (our_currtype != curr_type[f])
                            OnChanged(this, f);
                    }
                }
            })
        }));
    };
    for (auto&& expr : init_exprs) { lambda(expr); }
    for (auto&& expr : args) { lambda(expr); }
    GetType(boost::none);
}
Type* SourceExpression::GetType(InstanceKey f) {
    // Check the cache first.
    if (curr_type.find(boost::none) != curr_type.end())
        if (curr_type[boost::none] != nullptr)
            return curr_type[boost::none];
    if (curr_type.find(f) == curr_type.end())
        curr_type[f] = CalculateType(f);
    for(auto&& arg : exprs) {
        if (arg.second.types.find(f) == arg.second.types.end())
            arg.second.types[f] = arg.first->GetType(f);
        if (arg.second.types[f] == nullptr) {
            curr_type[f] = nullptr;
            break;
        }
    };
    return curr_type[f];
}
ResultExpression::ResultExpression(std::initializer_list<std::shared_ptr<Expression>> exprs)
    : SourceExpression(exprs) {}
ResultExpression::ResultExpression(std::initializer_list<std::shared_ptr<Expression>> exprs, const std::vector<std::shared_ptr<Expression>>& args)
    : SourceExpression(exprs, args) {}
bool ResultExpression::IsConstantExpression(InstanceKey f) {
    if (results.find(f) == results.end())
        CalculateType(f);
    return results[f].first->IsConstantExpression(f);
}
Type* ResultExpression::CalculateType(InstanceKey f) {
    if (results.find(f) == results.end()) {
        auto result = CalculateResult(f);
        auto result_connection = result->OnChanged.connect([this](Expression* e, InstanceKey f) {
            OnChanged(this, f);
        });
        results.insert(std::make_pair(f, std::make_pair(result, std::move(result_connection))));
    }
    return results[f].first->GetType(f);    
}

void Expression::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::String>(a.ExpressionHandlers, [](const Parse::String* str, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return Wide::Memory::MakeUnique<String>(str->val, a);
    });

    AddHandler<const Parse::MemberAccess>(a.ExpressionHandlers, [](const Parse::MemberAccess* memaccess, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        auto object = a.AnalyzeExpression(lookup, memaccess->expr.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) {
            auto access = Type::AccessMember(object, memaccess->mem, Context{ lookup, memaccess->location });
            if (!access) throw NoMember(object->GetType(f), lookup, memaccess->mem, memaccess->location);
            return access;
        });
    });

    AddHandler<const Parse::BooleanTest>(a.ExpressionHandlers, [](const Parse::BooleanTest* test, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return Type::BuildBooleanConversion(a.AnalyzeExpression(lookup, test->ex.get(), NonstaticLookup), { lookup, test->location });
    });

    AddHandler<const Parse::FunctionCall>(a.ExpressionHandlers, [](const Parse::FunctionCall* call, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        std::vector<std::shared_ptr<Expression>> args;
        for (auto&& arg : call->args)
            args.push_back(a.AnalyzeExpression(lookup, arg.get(), NonstaticLookup));
        auto object = a.AnalyzeExpression(lookup, call->callee.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) {
            return Type::BuildCall(object, args, Context{ lookup, call->location });
        });
    });

    AddHandler<const Parse::Identifier>(a.ExpressionHandlers, [](const Parse::Identifier* ident, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        return LookupIdentifier(lookup, ident->val, ident->location, ident->imp.get(), NonstaticLookup);
    });

    AddHandler<const Parse::True>(a.ExpressionHandlers, [](const Parse::True* tru, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(true, a);
    });

    AddHandler<const Parse::False>(a.ExpressionHandlers, [](const Parse::False* fals, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(false, a);
    });

    AddHandler<const Parse::This>(a.ExpressionHandlers, [](const Parse::This* thi, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return LookupIdentifier(lookup, "this", thi->location, nullptr, NonstaticLookup);
    });

    AddHandler<const Parse::Type>(a.ExpressionHandlers, [](const Parse::Type* ty, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto udt = a.GetUDT(ty, lookup->GetConstantContext() ? lookup->GetConstantContext() : lookup->GetContext(), "anonymous");
        return a.GetConstructorType(udt)->BuildValueConstruction({}, { lookup, ty->location });
    });


    AddHandler<const Parse::Integer>(a.ExpressionHandlers, [](const Parse::Integer* integer, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, std::stoll(integer->integral_value), true), a);
    });

    AddHandler<const Parse::BinaryExpression>(a.ExpressionHandlers, [](const Parse::BinaryExpression* bin, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto lhs = a.AnalyzeExpression(lookup, bin->lhs.get(), NonstaticLookup);
        auto rhs = a.AnalyzeExpression(lookup, bin->rhs.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) {
            return Type::BuildBinaryExpression(std::move(lhs), std::move(rhs), bin->type, { lookup, bin->location });
        });
    });

    AddHandler<const Parse::UnaryExpression>(a.ExpressionHandlers, [](const Parse::UnaryExpression* unex, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        auto expr = a.AnalyzeExpression(lookup, unex->ex.get(), NonstaticLookup);
        if (unex->type == &Lexer::TokenTypes::And)
            return CreateResultExpression([=, &a](InstanceKey f) { return Wide::Memory::MakeUnique<ImplicitAddressOf>(std::move(expr), Context(lookup, unex->location)); });
        return CreateResultExpression([=, &a](InstanceKey f) { return Type::BuildUnaryExpression(std::move(expr), unex->type, { lookup, unex->location }); });
    });


    AddHandler<const Parse::Increment>(a.ExpressionHandlers, [](const Parse::Increment* inc, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto expr = a.AnalyzeExpression(lookup, inc->ex.get(), NonstaticLookup);
        if (inc->postfix) {
            return CreateResultExpression([=, &a](InstanceKey f) {
                auto copy = expr->GetType(f)->Decay()->BuildValueConstruction({ expr }, { lookup, inc->location });
                auto result = Type::BuildUnaryExpression(expr, &Lexer::TokenTypes::Increment, { lookup, inc->location });
                return BuildChain(std::move(copy), BuildChain(std::move(result), copy));
            });
        }
        return CreateResultExpression([=, &a](InstanceKey f) { return Type::BuildUnaryExpression(std::move(expr), &Lexer::TokenTypes::Increment, { lookup, inc->location }); });
    });

    AddHandler<const Parse::Tuple>(a.ExpressionHandlers, [](const Parse::Tuple* tup, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        std::vector<std::shared_ptr<Expression>> exprs;
        for (auto&& elem : tup->expressions)
            exprs.push_back(a.AnalyzeExpression(lookup, elem.get(), NonstaticLookup));
        return CreateResultExpression([=, &a](InstanceKey f) {
            std::vector<Type*> types;
            for (auto&& expr : exprs)
                types.push_back(expr->GetType(f)->Decay());
            return a.GetTupleType(types)->ConstructFromLiteral(std::move(exprs), { lookup, tup->location });
        });
    });

    AddHandler<const Parse::PointerMemberAccess>(a.ExpressionHandlers, [](const Parse::PointerMemberAccess* paccess, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto subobj = Type::BuildUnaryExpression(a.AnalyzeExpression(lookup, paccess->ex.get(), NonstaticLookup), &Lexer::TokenTypes::Star, { lookup, paccess->location });
        return Type::AccessMember(subobj, paccess->member, { lookup, paccess->location });
    });

    AddHandler<const Parse::Decltype>(a.ExpressionHandlers, [](const Parse::Decltype* declty, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto expr = a.AnalyzeExpression(lookup, declty->ex.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) { return a.GetConstructorType(expr->GetType(f))->BuildValueConstruction({}, { lookup, declty->location }); });
    });

    AddHandler<const Parse::Typeid>(a.ExpressionHandlers, [](const Parse::Typeid* rtti, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup)  {
        auto expr = a.AnalyzeExpression(lookup, rtti->ex.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) -> std::shared_ptr<Expression> {
            auto tu = expr->GetType(f)->analyzer.AggregateCPPHeader("typeinfo", rtti->location);
            auto global_namespace = expr->GetType(f)->analyzer.GetClangNamespace(*tu, tu->GetDeclContext());
            auto std_namespace = Type::AccessMember(a.GetGlobalModule()->BuildValueConstruction({}, { lookup, rtti->location }), "std", { lookup, rtti->location });
            assert(std_namespace && "<typeinfo> didn't have std namespace?");
            auto clangty = Type::AccessMember(std::move(std_namespace), std::string("type_info"), { lookup, rtti->location });
            assert(clangty && "<typeinfo> didn't have std::type_info?");
            auto conty = dynamic_cast<ConstructorType*>(clangty->GetType(f)->Decay());
            assert(conty && "<typeinfo>'s std::type_info wasn't a type?");
            auto result = conty->analyzer.GetLvalueType(conty->GetConstructedType());
            // typeid(T)
            if (auto ty = dynamic_cast<ConstructorType*>(expr->GetType(f)->Decay())) {
                struct RTTI : public Expression {
                    RTTI(Type* ty, Type* result) : ty(ty), result(result) { rtti = ty->GetRTTI(); }
                    Type* ty;
                    Type* result;
                    std::function<llvm::Constant*(llvm::Module*)> rtti;
                    Type* GetType(InstanceKey f) override final { return result; }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        return con->CreateBitCast(rtti(con), result->GetLLVMType(con));
                    }
                };
                return Wide::Memory::MakeUnique<RTTI>(ty->GetConstructedType(), result);
            }
            // typeid(expr)
            struct RTTI : public Expression {
                RTTI(std::shared_ptr<Expression> arg, Type* result, InstanceKey f) : expr(std::move(arg)), result(result)
                {
                    // If we have a polymorphic type, find the RTTI entry, if applicable.
                    ty = expr->GetType(f)->Decay();
                    vtable = ty->GetVtableLayout();
                    if (!vtable.layout.empty()) {
                        expr = Type::GetVirtualPointer(std::move(expr));
                        for (unsigned int i = 0; i < vtable.layout.size(); ++i) {
                            if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&vtable.layout[i].func)) {
                                if (*spec == Type::VTableLayout::SpecialMember::RTTIPointer) {
                                    rtti_offset = i - vtable.offset;
                                    break;
                                }
                            }
                        }
                    }
                    if (!rtti_offset)
                        rtti = ty->GetRTTI();
                }
                std::function<llvm::Constant*(llvm::Module*)> rtti;
                std::shared_ptr<Expression> expr;
                Type::VTableLayout vtable;
                Wide::Util::optional<unsigned> rtti_offset;
                Type* result;
                Type* ty;
                Type* GetType(InstanceKey f) override final { return result; }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    // Do we have a vtable offset? If so, use the RTTI entry there. The expr will already be a pointer to the vtable pointer.
                    if (rtti_offset) {
                        auto vtable_pointer = con->CreateLoad(expr->GetValue(con));
                        auto rtti_pointer = con->CreateLoad(con->CreateConstGEP1_32(vtable_pointer, *rtti_offset));
                        return con->CreateBitCast(rtti_pointer, result->GetLLVMType(con));
                    }
                    return con->CreateBitCast(rtti(con), result->GetLLVMType(con));
                }
            };
            return Wide::Memory::MakeUnique<RTTI>(std::move(expr), result, f);
        });
    });

    AddHandler<const Parse::Lambda>(a.ExpressionHandlers, [](const Parse::Lambda* lam, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        std::unordered_map<Parse::Name, std::shared_ptr<Expression>> implicit_captures;
        std::unordered_set<Parse::Name> explicit_captures;
        for (auto&& cap : lam->Captures) {
            explicit_captures.insert(cap.name.front().name);
        }
        auto skeleton = a.GetWideFunction(lam, lookup, "lambda at " + to_string(lam->where), [&](Parse::Name name, Lexer::Range where) -> std::shared_ptr<Expression> {
            auto access = [&] {
                auto self = NonstaticLookup("this", where);
                return CreateResultExpression([=](Expression::InstanceKey) {
                    return Type::AccessMember(self, name, { lookup, where });
                });
            };
            if (explicit_captures.find(name) != explicit_captures.end()) {
                return access();
            }
            if (implicit_captures.find(name) != implicit_captures.end())
                return implicit_captures[name];
            if (auto result = NonstaticLookup(name, where)) {
                implicit_captures[name] = result;
                return access();
            }
            return nullptr;
        }); 
        skeleton->ComputeBody();
        
        Context c(lookup, lam->location);
        std::vector<std::pair<Parse::Name, std::shared_ptr<Expression>>> cap_expressions;
        for (auto&& arg : lam->Captures) {
            cap_expressions.push_back(std::make_pair(arg.name.front().name, a.AnalyzeExpression(lookup, arg.initializer.get(), NonstaticLookup)));
        }
        for (auto&& name : implicit_captures) {
            cap_expressions.push_back(std::make_pair(name.first, name.second));
        }
        
        return CreateResultExpression([=, &a](InstanceKey f) {
            std::vector<std::pair<Parse::Name, Type*>> types;
            std::vector<std::shared_ptr<Expression>> expressions;
            for (auto&& cap : cap_expressions) {
                if (!lam->defaultref)
                    types.push_back(std::make_pair(cap.first, cap.second->GetType(f)->Decay()));
                else {
                    if (implicit_captures.find(cap.first) != implicit_captures.end()) {
                        if (!cap.second->GetType(f)->IsReference())
                            assert(false); // how the fuck
                        types.push_back(std::make_pair(cap.first, cap.second->GetType(f)));
                    } else {
                        types.push_back(std::make_pair(cap.first, cap.second->GetType(f)->Decay()));
                    }
                }
                expressions.push_back(std::move(cap.second));
            }
            auto type = a.GetLambdaType(lam, types, lookup);
            return type->BuildLambdaFromCaptures(std::move(expressions), c);
        });
    });

    AddHandler<const Parse::DynamicCast>(a.ExpressionHandlers, [](const Parse::DynamicCast* dyn_cast, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        auto type = a.AnalyzeExpression(lookup, dyn_cast->type.get(), NonstaticLookup);
        auto object = a.AnalyzeExpression(lookup, dyn_cast->object.get(), NonstaticLookup);

        auto dynamic_cast_to_void = [&](PointerType* baseptrty, InstanceKey f) -> std::shared_ptr<Expression> {
            // Load it from the vtable if it actually has one.
            auto layout = baseptrty->GetPointee()->GetVtableLayout();
            if (layout.layout.size() == 0) {
                throw std::runtime_error("dynamic_casted to void* a non-polymorphic type.");
            }
            for (unsigned int i = 0; i < layout.layout.size(); ++i) {
                if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&layout.layout[i].func)) {
                    if (*spec == Type::VTableLayout::SpecialMember::OffsetToTop) {
                        auto offset = i - layout.offset;
                        auto vtable = Type::GetVirtualPointer(object);
                        struct DynamicCastToVoidPointer : Expression {
                            DynamicCastToVoidPointer(unsigned off, std::shared_ptr<Expression> obj, std::shared_ptr<Expression> vtable)
                                : vtable_offset(off), vtable(std::move(vtable)), object(std::move(obj)) {}
                            unsigned vtable_offset;
                            std::shared_ptr<Expression> vtable;
                            std::shared_ptr<Expression> object;
                            llvm::Value* ComputeValue(CodegenContext& con) override final {
                                auto obj_ptr = object->GetValue(con);
                                llvm::BasicBlock* source_bb = con->GetInsertBlock();
                                llvm::BasicBlock* nonnull_bb = llvm::BasicBlock::Create(con, "nonnull_bb", source_bb->getParent());
                                llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", source_bb->getParent());
                                con->CreateCondBr(con->CreateIsNull(obj_ptr), continue_bb, nonnull_bb);
                                con->SetInsertPoint(nonnull_bb);
                                auto vtable_ptr = con->CreateLoad(vtable->GetValue(con));
                                auto ptr_to_offset = con->CreateConstGEP1_32(vtable_ptr, vtable_offset);
                                auto offset = con->CreateLoad(ptr_to_offset);
                                auto result = con->CreateGEP(con->CreateBitCast(obj_ptr, con.GetInt8PtrTy()), offset);
                                con->CreateBr(continue_bb);
                                con->SetInsertPoint(continue_bb);
                                auto phi = con->CreatePHI(con.GetInt8PtrTy(), 2);
                                phi->addIncoming(llvm::Constant::getNullValue(con.GetInt8PtrTy()), source_bb);
                                phi->addIncoming(result, nonnull_bb);
                                return phi;
                            }
                            Type* GetType(InstanceKey f) override final {
                                auto&& a = object->GetType(f)->analyzer;
                                return a.GetPointerType(a.GetVoidType());
                            }
                        };
                        return Wide::Memory::MakeUnique<DynamicCastToVoidPointer>(offset, std::move(object), std::move(vtable));
                    }
                }
            }
            throw std::runtime_error("Attempted to cast to void*, but the object's vtable did not carry an offset to top member.");
        };

        auto polymorphic_dynamic_cast = [&](PointerType* basety, PointerType* derty, InstanceKey f) -> std::shared_ptr<Expression> {
            struct PolymorphicDynamicCast : Expression {
                PolymorphicDynamicCast(Type* basety, Type* derty, std::shared_ptr<Expression> object)
                    : basety(basety), derty(derty), object(std::move(object))
                {
                    basertti = basety->GetRTTI();
                    derrtti = derty->GetRTTI();
                }
                std::shared_ptr<Expression> object;
                Type* basety;
                Type* derty;
                std::function<llvm::Constant*(llvm::Module*)> basertti;
                std::function<llvm::Constant*(llvm::Module*)> derrtti;
                Type* GetType(InstanceKey f) override final {
                    return derty->analyzer.GetPointerType(derty);
                }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto obj_ptr = object->GetValue(con);
                    llvm::BasicBlock* source_bb = con->GetInsertBlock();
                    llvm::BasicBlock* nonnull_bb = llvm::BasicBlock::Create(con, "nonnull_bb", source_bb->getParent());
                    llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", source_bb->getParent());
                    con->CreateCondBr(con->CreateIsNull(obj_ptr), continue_bb, nonnull_bb);
                    con->SetInsertPoint(nonnull_bb);
                    auto dynamic_cast_func = con.module->getFunction("__dynamic_cast");
                    auto ptrdiffty = llvm::IntegerType::get(con, basety->analyzer.GetDataLayout().getPointerSize());
                    if (!dynamic_cast_func) {
                        llvm::Type* args[] = { con.GetInt8PtrTy(), con.GetInt8PtrTy(), con.GetInt8PtrTy(), ptrdiffty };
                        auto functy = llvm::FunctionType::get(llvm::Type::getVoidTy(con), args, false);
                        dynamic_cast_func = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__dynamic_cast", con);
                    }
                    llvm::Value* args[] = { obj_ptr, basertti(con), derrtti(con), llvm::ConstantInt::get(ptrdiffty, (uint64_t)-1, true) };
                    auto result = con->CreateCall(dynamic_cast_func, args, "");
                    con->CreateBr(continue_bb);
                    con->SetInsertPoint(continue_bb);
                    auto phi = con->CreatePHI(con.GetInt8PtrTy(), 2);
                    phi->addIncoming(llvm::Constant::getNullValue(derty->GetLLVMType(con)), source_bb);
                    phi->addIncoming(result, nonnull_bb);
                    return con->CreatePointerCast(phi, GetType(nullptr)->GetLLVMType(con));
                }
            };
            return Wide::Memory::MakeUnique<PolymorphicDynamicCast>(basety->GetPointee(), derty->GetPointee(), std::move(object));
        };

        return CreateResultExpression([=](InstanceKey f) {
            if (auto con = dynamic_cast<ConstructorType*>(type->GetType(f)->Decay())) {
                // Only support pointers right now
                if (auto derptrty = dynamic_cast<PointerType*>(con->GetConstructedType())) {
                    if (auto baseptrty = dynamic_cast<PointerType*>(object->GetType(f)->Decay())) {
                        // derived-to-base conversion- doesn't require calling the routine
                        if (baseptrty->GetPointee()->IsDerivedFrom(derptrty->GetPointee()) == Type::InheritanceRelationship::UnambiguouslyDerived) {
                            return derptrty->BuildValueConstruction({ object }, { lookup, dyn_cast->location });
                        }

                        // void*
                        if (derptrty->GetPointee() == con->analyzer.GetVoidType()) {
                            return dynamic_cast_to_void(baseptrty, f);
                        }

                        // polymorphic
                        if (baseptrty->GetPointee()->GetVtableLayout().layout.empty())
                            throw std::runtime_error("Attempted dynamic_cast on non-polymorphic base.");

                        return polymorphic_dynamic_cast(baseptrty, derptrty, f);
                    }
                }
            }
            throw std::runtime_error("Used unimplemented dynamic_cast functionality.");
        });
    });

    AddHandler<const Parse::Index>(a.ExpressionHandlers, [](const Parse::Index* index, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
        auto obj = a.AnalyzeExpression(lookup, index->object.get(), NonstaticLookup);
        auto ind = a.AnalyzeExpression(lookup, index->index.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) {
            auto ty = obj->GetType(f);
            return Type::BuildIndex(std::move(obj), std::move(ind), { lookup, index->location });
        });
    });

    AddHandler<const Parse::DestructorAccess>(a.ExpressionHandlers, [](const Parse::DestructorAccess* des, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr<Expression> {
        auto object = a.AnalyzeExpression(lookup, des->expr.get(), NonstaticLookup);
        return CreateResultExpression([=, &a](InstanceKey f) {
            auto ty = object->GetType(f);
            return std::make_shared<DestructorCall>(ty->Decay()->BuildDestructorCall(std::move(object), { lookup, des->location }, false), a);
        });
    });

    AddHandler<const Parse::GlobalModuleReference>(a.ExpressionHandlers, [](const Parse::GlobalModuleReference* globmod, Analyzer& a, Type* lookup, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) -> std::shared_ptr < Expression > {
        return a.GetGlobalModule()->BuildValueConstruction({}, { lookup, globmod->location });
    });
}