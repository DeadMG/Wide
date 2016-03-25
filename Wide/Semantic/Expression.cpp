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
#include <Wide/Util/DebugBreak.h>

using namespace Wide;
using namespace Semantic;

ImplicitLoadExpr::ImplicitLoadExpr(std::shared_ptr<Expression> arg)
    : SourceExpression(Range::Elements(arg)), src(std::move(arg)) {}
Type* ImplicitLoadExpr::CalculateType() {
    assert(src->GetType()->IsReference());
    return src->GetType()->Decay();
}
llvm::Value* ImplicitLoadExpr::ComputeValue(CodegenContext& con) {
    return con->CreateLoad(src->GetValue(con));
}

ImplicitStoreExpr::ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value)
    : SourceExpression(Range::Elements( memory, value)), mem(std::move(memory)), val(std::move(value)) {}
Type* ImplicitStoreExpr::CalculateType() {
    assert(mem->GetType()->IsReference(val->GetType()));
    return mem->GetType();
}
llvm::Value* ImplicitStoreExpr::ComputeValue(CodegenContext& con) {
    auto memory = mem->GetValue(con);
    auto value = val->GetValue(con);
    con->CreateStore(value, memory);
    return memory;
}
void Expression::Instantiate(Function* f) {
    GetType();
}

LvalueCast::LvalueCast(std::shared_ptr<Expression> expr)
    : SourceExpression(Range::Elements(expr)), expr(std::move(expr)) {}
Type* LvalueCast::CalculateType() {
    assert(IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetLvalueType(expr->GetType()->Decay());
}
llvm::Value* LvalueCast::ComputeValue(CodegenContext& con) {
    return expr->GetValue(con);
}

RvalueCast::RvalueCast(std::shared_ptr<Expression> ex)
    : SourceExpression(Range::Elements(ex)), expr(std::move(ex)) {}
Type* RvalueCast::CalculateType() {
    assert(!IsRvalueType(expr->GetType()));
    return expr->GetType()->analyzer.GetRvalueType(expr->GetType()->Decay());
}
llvm::Value* RvalueCast::ComputeValue(CodegenContext& con) {
    if (IsLvalueType(expr->GetType()))
        return expr->GetValue(con);
    if (expr->GetType()->AlwaysKeepInMemory(con))
        return expr->GetValue(con);
    assert(!IsRvalueType(expr->GetType()));
    auto tempalloc = con.CreateAlloca(expr->GetType());
    con->CreateStore(expr->GetValue(con), tempalloc);
    return tempalloc;
}
String::String(std::string str, Analyzer& a)
: str(std::move(str)), a(a){}
Type* String::GetType() {
    return a.GetLiteralStringType();
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
    return CreatePrimGlobal(Range::Elements( self ), ret, [=](CodegenContext& con) {
        return func(self->GetValue(con), con);
    });
}
bool SourceExpression::IsConstantExpression() {
    for (auto&& expr : exprs)
        if (!expr.first->IsConstant())
            return false;
    return true;
}
std::shared_ptr<Expression> Semantic::CreatePrimGlobal(Wide::Range::Erased<std::shared_ptr<Expression>> deps, Type* ret, std::function<llvm::Value*(CodegenContext& con)> func) {
    struct PrimGlobalOp : Expression {
        PrimGlobalOp(Wide::Range::Erased<std::shared_ptr<Expression>> deps, Type* r, std::function<llvm::Value*(CodegenContext& con)> func)
        : ret(std::move(r)), action(std::move(func)) 
        {
            deps | Range::Copy([this](std::shared_ptr<Expression> dep) {
                exprs.insert(dep);
            });
            for (auto&& expr : exprs)
                assert(expr);
        }

        std::unordered_set<std::shared_ptr<Expression>> exprs;

        Type* ret;
        std::function<llvm::Value*(CodegenContext& con)> action;

        Type* GetType() override final {
            for (auto expr : exprs)
                expr->GetType();
            return ret;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto val = action(con);
            if (ret != ret->analyzer.GetVoidType())
                assert(val);
            return val;
        }
        bool IsConstantExpression() override final {
            bool is = true;
            for (auto expr : exprs)
                if (!expr->IsConstant())
                    is = false;
            return is;
        }
    };
    return Wide::Memory::MakeUnique<PrimGlobalOp>(deps, ret, func);
}
std::shared_ptr<Expression> Semantic::CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return CreatePrimOp(std::move(lhs), std::move(rhs), lhs->GetType(), func);
}
std::shared_ptr<Expression> Semantic::CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    return Wide::Memory::MakeUnique<ImplicitStoreExpr>(lhs, CreatePrimOp(Wide::Memory::MakeUnique<ImplicitLoadExpr>(lhs), std::move(rhs), func));
}
std::shared_ptr<Expression> Semantic::CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext& con)> func) {
    assert(ret != nullptr);
    return CreatePrimGlobal(Range::Elements(lhs, rhs), ret, [=](CodegenContext& con) {
        auto left = lhs->GetValue(con);
        auto right = rhs->GetValue(con);
        auto val = func(left, right, con);
        if (ret != ret->analyzer.GetVoidType())
            assert(val);
        return val;
    });
}
std::shared_ptr<Expression> Semantic::BuildValue(std::shared_ptr<Expression> e) {
    return CreateResultExpression(Range::Elements(e), [=]() -> std::shared_ptr<Expression> {
        if (e->GetType()->IsReference())
            return std::make_shared<ImplicitLoadExpr>(std::move(e));
        return e;
    });
}
Chain::Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result)
    : SourceExpression(Range::Elements(effect, result)), SideEffect(std::move(effect)), result(std::move(result)) {}
Type* Chain::CalculateType() {
    return result->GetType();
}
llvm::Value* Chain::ComputeValue(CodegenContext& con) {
    SideEffect->GetValue(con);
    return result->GetValue(con);
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
    auto selfty = GetType()->GetLLVMType(con);
    if (GetType()->AlwaysKeepInMemory(con)) {
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
    std::string err;
    llvm::raw_string_ostream strostream(err);
    if (llvm::verifyFunction(*func, &strostream)) {
        strostream.flush();
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
    }
}
CodegenContext::CodegenContext(llvm::Module* mod, llvm::IRBuilder<>& alloc_builder, llvm::IRBuilder<>& gep_builder, llvm::IRBuilder<>& ir_builder)
    : module(mod), alloca_builder(&alloc_builder), gep_builder(&gep_builder), insert_builder(&ir_builder)
{
    gep_map = std::make_shared<std::unordered_map<llvm::AllocaInst*, std::unordered_map<unsigned, llvm::Value*>>>();
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
SourceExpression::SourceExpression(Wide::Range::Erased<std::shared_ptr<Expression>> range) {
    range | Range::Copy([this](std::shared_ptr<Expression> expr) {
        if (expr == nullptr) return;
        exprs.insert(std::make_pair(expr, ExpressionData(
            boost::none,
            expr->OnChanged.connect([this](Expression* e) {
                auto newtype = e->GetType();
                for (auto&& expr : exprs) {
                    if (expr.first.get() == e) {
                        expr.second.type = newtype;
                    }
                }
                if (!curr_type.is_initialized())
                    return;
                auto our_currtype = curr_type;
                curr_type = CalculateType();
                if (our_currtype != curr_type)
                    OnChanged(this);
            })
        )));
    });
}
Type* SourceExpression::GetType() {
    if (curr_type.is_initialized())
        return *curr_type;

    // Welp not here. Calculate the type if we have the appropriate arguments.
    bool dont = false;
    for(auto&& arg : exprs) {
        if (!arg.second.type.is_initialized())
            arg.second.type = arg.first->GetType();
        if (*arg.second.type == nullptr) {
            dont = true;
            //break;
        }
    };
    if (dont) return *(curr_type = nullptr);
    return *(curr_type = CalculateType());
}
ResultExpression::ResultExpression(Wide::Range::Erased<std::shared_ptr<Expression>> exprs)
    : SourceExpression(exprs) {}
Type* ResultExpression::CalculateType() {
    if (result.is_initialized())
        return result->expr->GetType();

    auto result_expr = CalculateResult();
    auto result_connection = result_expr->OnChanged.connect([this](Expression* e) {
        OnChanged(this);
    });
    result = Result{
        result_expr,
        std::move(result_connection)
    };
    return result_expr->GetType();
}
llvm::Value* ResultExpression::ComputeValue(CodegenContext& con) {
    return result->expr->GetValue(con);
}

void Expression::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::String>(a.ExpressionHandlers, [](const Parse::String* str, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        return Wide::Memory::MakeUnique<String>(str->val, a);
    });

    AddHandler<const Parse::MemberAccess>(a.ExpressionHandlers, [](const Parse::MemberAccess* memaccess, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        auto object = a.AnalyzeExpression(lookup, memaccess->expr.get(), _this);
        return CreateResultExpression(Range::Elements(object), [=, &a]() {
            auto access = Type::AccessMember(object, memaccess->mem, Context{ lookup, memaccess->location });
            if (!access) return CreateErrorExpression(Memory::MakeUnique<SpecificError<NoMemberFound>>(a, memaccess->location, "Could not find member."));
            return access;
        });
    });

    AddHandler<const Parse::BooleanTest>(a.ExpressionHandlers, [](const Parse::BooleanTest* test, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto sub_expr = a.AnalyzeExpression(lookup, test->ex.get(), _this); 
        return CreateResultExpression(Range::Elements(sub_expr), [=]() {
            return Type::BuildBooleanConversion(sub_expr, { lookup, test->location });
        });
    });

    AddHandler<const Parse::FunctionCall>(a.ExpressionHandlers, [](const Parse::FunctionCall* call, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        std::vector<std::shared_ptr<Expression>> args;
        for (auto&& arg : call->args)
            args.push_back(a.AnalyzeExpression(lookup, arg.get(), _this));
        auto object = a.AnalyzeExpression(lookup, call->callee.get(), _this);
        return CreateResultExpression(Range::Elements(object) | Range::Concat(Range::Container(args)), [=, &a]() {
            return Type::BuildCall(object, args, Context{ lookup, call->location });
        });
    });

    AddHandler<const Parse::Identifier>(a.ExpressionHandlers, [](const Parse::Identifier* ident, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        auto val = LookupName(lookup, ident->val, ident->location, _this, ident->imp.get());
        if (!val) throw SpecificError<IdentifierLookupFailed>(a, ident->location, "Could not find identifier.");
        return val;
    });

    AddHandler<const Parse::True>(a.ExpressionHandlers, [](const Parse::True* tru, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(true, a);
    });

    AddHandler<const Parse::False>(a.ExpressionHandlers, [](const Parse::False* fals, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(false, a);
    });

    AddHandler<const Parse::This>(a.ExpressionHandlers, [](const Parse::This* thi, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        if (_this == nullptr)
            throw SpecificError<IdentifierLookupFailed>(a, thi->location, "Used this in a non-member context.");
        return _this;
    });

    AddHandler<const Parse::Type>(a.ExpressionHandlers, [](const Parse::Type* ty, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto udt = a.GetUDT(ty, lookup, "anonymous");
        return a.GetConstructorType(udt)->BuildValueConstruction({}, { lookup, ty->location });
    });


    AddHandler<const Parse::Integer>(a.ExpressionHandlers, [](const Parse::Integer* integer, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, std::stoll(integer->integral_value), true), a);
    });

    AddHandler<const Parse::BinaryExpression>(a.ExpressionHandlers, [](const Parse::BinaryExpression* bin, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto lhs = a.AnalyzeExpression(lookup, bin->lhs.get(), _this);
        auto rhs = a.AnalyzeExpression(lookup, bin->rhs.get(), _this);
        return CreateResultExpression(Range::Elements(lhs, rhs), [=]() {
            return Type::BuildBinaryExpression(std::move(lhs), std::move(rhs), bin->type, { lookup, bin->location });
        });
    });

    AddHandler<const Parse::UnaryExpression>(a.ExpressionHandlers, [](const Parse::UnaryExpression* unex, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        auto expr = a.AnalyzeExpression(lookup, unex->ex.get(), _this);
        if (unex->type == &Lexer::TokenTypes::And)
            return CreateAddressOf(std::move(expr), Context(lookup, unex->location));
        return CreateResultExpression(Range::Elements(expr), [=]() {
            return Type::BuildUnaryExpression(std::move(expr), unex->type, { lookup, unex->location });
        });
    });


    AddHandler<const Parse::Increment>(a.ExpressionHandlers, [](const Parse::Increment* inc, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto expr = a.AnalyzeExpression(lookup, inc->ex.get(), _this);
        return CreateResultExpression(Range::Elements(expr), [=, &a]() {
            if (inc->postfix) {
                auto copy = expr->GetType()->Decay()->BuildValueConstruction({ expr }, { lookup, inc->location });
                auto result = Type::BuildUnaryExpression(expr, &Lexer::TokenTypes::Increment, { lookup, inc->location });
                return BuildChain(std::move(copy), BuildChain(std::move(result), copy));
            }
            return Type::BuildUnaryExpression(std::move(expr), &Lexer::TokenTypes::Increment, { lookup, inc->location });
        });
    });

    AddHandler<const Parse::Tuple>(a.ExpressionHandlers, [](const Parse::Tuple* tup, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        std::vector<std::shared_ptr<Expression>> exprs;
        for (auto&& elem : tup->expressions)
            exprs.push_back(a.AnalyzeExpression(lookup, elem.get(), _this));
        return CreateResultExpression(Range::Container(exprs), [=, &a]() {
            std::vector<Type*> types;
            for (auto&& expr : exprs)
                types.push_back(expr->GetType()->Decay());
            return a.GetTupleType(types)->ConstructFromLiteral(std::move(exprs), { lookup, tup->location });
        });
    });

    AddHandler<const Parse::PointerMemberAccess>(a.ExpressionHandlers, [](const Parse::PointerMemberAccess* paccess, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto source = a.AnalyzeExpression(lookup, paccess->ex.get(), _this);
        return CreateResultExpression(Range::Elements(source), [=]() {
            auto subobj = Type::BuildUnaryExpression(source, &Lexer::TokenTypes::Star, { lookup, paccess->location });
            return Type::AccessMember(subobj, paccess->member, { lookup, paccess->location });
        });
    });

    AddHandler<const Parse::Decltype>(a.ExpressionHandlers, [](const Parse::Decltype* declty, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto expr = a.AnalyzeExpression(lookup, declty->ex.get(), _this);
        return CreateResultExpression(Range::Elements(expr), [=, &a]() { return a.GetConstructorType(expr->GetType())->BuildValueConstruction({}, { lookup, declty->location }); });
    });

    AddHandler<const Parse::Typeid>(a.ExpressionHandlers, [](const Parse::Typeid* rtti, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this)  {
        auto expr = a.AnalyzeExpression(lookup, rtti->ex.get(), _this);
        return CreateResultExpression(Range::Elements(expr), [=, &a]() -> std::shared_ptr<Expression> {
            auto tu = a.AggregateCPPHeader("typeinfo", rtti->location);
            auto global_namespace = a.GetClangNamespace(*tu, Location(a, tu), tu->GetDeclContext());
            auto std_namespace = Type::AccessMember(a.GetGlobalModule()->BuildValueConstruction({}, { lookup, rtti->location }), "std", { lookup, rtti->location });
            assert(std_namespace && "<typeinfo> didn't have std namespace?");
            auto clangty = Type::AccessMember(std::move(std_namespace), std::string("type_info"), { lookup, rtti->location });
            assert(clangty && "<typeinfo> didn't have std::type_info?");
            auto conty = dynamic_cast<ConstructorType*>(clangty->GetType()->Decay());
            assert(conty && "<typeinfo>'s std::type_info wasn't a type?");
            auto result = conty->analyzer.GetLvalueType(conty->GetConstructedType());
            // typeid(T)
            if (auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
                auto rtti = ty->GetConstructedType()->GetRTTI();
                return CreatePrimGlobal(Range::Elements(expr), result, [=](CodegenContext& con) {
                    return con->CreateBitCast(rtti(con), result->GetLLVMType(con));
                });
            }
            auto ty = expr->GetType()->Decay();
            auto vtable = ty->GetVtableLayout();
            if (!vtable.layout.empty()) {
                auto vptr = Type::GetVirtualPointer(std::move(expr));
                for (unsigned int i = 0; i < vtable.layout.size(); ++i) {
                    if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&vtable.layout[i].func)) {
                        if (*spec == Type::VTableLayout::SpecialMember::RTTIPointer) {
                            auto rtti_offset = i - vtable.offset;
                            return CreatePrimGlobal(Range::Elements(vptr), result, [=](CodegenContext& con) {
                                auto vtable_pointer = con->CreateLoad(vptr->GetValue(con));
                                auto rtti_pointer = con->CreateLoad(con->CreateConstGEP1_32(vtable_pointer, rtti_offset));
                                return con->CreateBitCast(rtti_pointer, result->GetLLVMType(con));
                            });
                        }
                    }
                }
            }
            auto rtti = ty->GetRTTI();
            return CreatePrimGlobal(Range::Elements(expr), result, [=](CodegenContext& con) {
                return con->CreateBitCast(rtti(con), result->GetLLVMType(con));
            });
        });
    });

    AddHandler<const Parse::Lambda>(a.ExpressionHandlers, [](const Parse::Lambda* lam, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        std::unordered_set<Parse::Name> lambda_locals;
        auto captures = GetLambdaCaptures(lam, a, lambda_locals);
        Context c(lookup, lam->location);
        std::vector<std::pair<Parse::Name, std::shared_ptr<Expression>>> cap_expressions;
        for (auto&& arg : lam->Captures) {
            cap_expressions.push_back(std::make_pair(arg.name.front().name, a.AnalyzeExpression(lookup, arg.initializer.get(), _this)));
        }
        for (auto&& name : captures) {
            cap_expressions.push_back(std::make_pair(name, LookupName(lookup, name, lam->location, _this, nullptr)));
        }
        
        //return std::shared_ptr<Expression>();
        return CreateResultExpression(
            Range::Container(cap_expressions) | Range::Map([](std::pair<Parse::Name, std::shared_ptr<Expression>> expr) { return expr.second; }),
            [=, &a]() {
                std::vector<std::pair<Parse::Name, Type*>> types;
                std::vector<std::shared_ptr<Expression>> expressions;
                for (auto&& cap : cap_expressions) {
                    if (!lam->defaultref)
                        types.push_back(std::make_pair(cap.first, cap.second->GetType()->Decay()));
                    else {
                        if (captures.find(cap.first) != captures.end()) {
                            if (!cap.second->GetType()->IsReference())
                                assert(false); // how the fuck
                            types.push_back(std::make_pair(cap.first, cap.second->GetType()));
                        } else {
                            types.push_back(std::make_pair(cap.first, cap.second->GetType()->Decay()));
                        }
                    }
                    expressions.push_back(std::move(cap.second));
                }
                auto type = a.GetLambdaType(lam, lookup, types);
                return type->BuildLambdaFromCaptures(std::move(expressions), c);
            }
        );
    });

    AddHandler<const Parse::DynamicCast>(a.ExpressionHandlers, [](const Parse::DynamicCast* dyn_cast, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        auto type = a.AnalyzeExpression(lookup, dyn_cast->type.get(), _this);
        auto object = a.AnalyzeExpression(lookup, dyn_cast->object.get(), _this);

        auto dynamic_cast_to_void = [&](PointerType* baseptrty) -> std::shared_ptr<Expression> {
            // Load it from the vtable if it actually has one.
            auto layout = baseptrty->GetPointee()->GetVtableLayout();
            if (layout.layout.size() == 0) {
                throw SpecificError<DynamicCastNotPolymorphicType>(a, dyn_cast->location, "dynamic_casted to void* a non-polymorphic type.");
            }
            for (unsigned int i = 0; i < layout.layout.size(); ++i) {
                if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&layout.layout[i].func)) {
                    if (*spec == Type::VTableLayout::SpecialMember::OffsetToTop) {
                        auto offset = i - layout.offset;
                        auto vtable = Type::GetVirtualPointer(object);
                        return CreatePrimGlobal(Range::Elements(type, object), a.GetPointerType(a.GetVoidType()), [=](CodegenContext& con) {
                            auto obj_ptr = object->GetValue(con);
                            llvm::BasicBlock* source_bb = con->GetInsertBlock();
                            llvm::BasicBlock* nonnull_bb = llvm::BasicBlock::Create(con, "nonnull_bb", source_bb->getParent());
                            llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", source_bb->getParent());
                            con->CreateCondBr(con->CreateIsNull(obj_ptr), continue_bb, nonnull_bb);
                            con->SetInsertPoint(nonnull_bb);
                            auto vtable_ptr = con->CreateLoad(vtable->GetValue(con));
                            auto ptr_to_offset = con->CreateConstGEP1_32(vtable_ptr, offset);
                            auto offset = con->CreateLoad(ptr_to_offset);
                            auto result = con->CreateGEP(con->CreateBitCast(obj_ptr, con.GetInt8PtrTy()), offset);
                            con->CreateBr(continue_bb);
                            con->SetInsertPoint(continue_bb);
                            auto phi = con->CreatePHI(con.GetInt8PtrTy(), 2);
                            phi->addIncoming(llvm::Constant::getNullValue(con.GetInt8PtrTy()), source_bb);
                            phi->addIncoming(result, nonnull_bb);
                            return phi;
                        });
                    }
                }
            }
            throw SpecificError<VTableLayoutIncompatible>(a, dyn_cast->location, "Attempted to cast to void*, but the object's vtable did not carry an offset to top member.");
        };

        auto polymorphic_dynamic_cast = [&](PointerType* basety, PointerType* derty) -> std::shared_ptr<Expression> {
            auto basertti = basety->GetPointee()->GetRTTI();
            auto derrtti = derty->GetPointee()->GetRTTI();
            return CreatePrimGlobal(Range::Elements(type, object), derty, [=](CodegenContext& con) {
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
                return con->CreatePointerCast(phi, derty->GetLLVMType(con));
            });
        };

        return CreateResultExpression(Range::Elements(type, object), [=, &a]() {
            if (auto con = dynamic_cast<ConstructorType*>(type->GetType()->Decay())) {
                // Only support pointers right now
                if (auto derptrty = dynamic_cast<PointerType*>(con->GetConstructedType())) {
                    if (auto baseptrty = dynamic_cast<PointerType*>(object->GetType()->Decay())) {
                        // derived-to-base conversion- doesn't require calling the routine
                        if (baseptrty->GetPointee()->IsDerivedFrom(derptrty->GetPointee()) == Type::InheritanceRelationship::UnambiguouslyDerived) {
                            return derptrty->BuildValueConstruction({ object }, { lookup, dyn_cast->location });
                        }

                        // void*
                        if (derptrty->GetPointee() == con->analyzer.GetVoidType()) {
                            return dynamic_cast_to_void(baseptrty);
                        }

                        // polymorphic
                        if (baseptrty->GetPointee()->GetVtableLayout().layout.empty())
                            throw SpecificError<DynamicCastNotPolymorphicType>(a, dyn_cast->location, "Attempted dynamic_cast on non-polymorphic base.");

                        return polymorphic_dynamic_cast(baseptrty, derptrty);
                    }
                }
            }
            throw SpecificError<UnimplementedDynamicCast>(a, dyn_cast->location, "Used unimplemented dynamic_cast functionality.");
        });
    });

    AddHandler<const Parse::Index>(a.ExpressionHandlers, [](const Parse::Index* index, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) {
        auto obj = a.AnalyzeExpression(lookup, index->object.get(), _this);
        auto ind = a.AnalyzeExpression(lookup, index->index.get(), _this);
        return CreateResultExpression(Range::Elements(obj, ind), [=, &a]() {
            return Type::BuildIndex(std::move(obj), std::move(ind), { lookup, index->location });
        });
    });

    AddHandler<const Parse::DestructorAccess>(a.ExpressionHandlers, [](const Parse::DestructorAccess* des, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr<Expression> {
        auto object = a.AnalyzeExpression(lookup, des->expr.get(), _this);
        return CreateResultExpression(Range::Elements(object), [=, &a]() {
            auto ty = object->GetType();
            auto destructor = ty->Decay()->BuildDestructorCall(std::move(object), { lookup, des->location }, false);
            return CreatePrimGlobal(Range::Elements(object), a, destructor);
        });
    });

    AddHandler<const Parse::GlobalModuleReference>(a.ExpressionHandlers, [](const Parse::GlobalModuleReference* globmod, Analyzer& a, Location lookup, std::shared_ptr<Expression> _this) -> std::shared_ptr < Expression > {
        return a.GetGlobalModule()->BuildValueConstruction( {}, { lookup, globmod->location });
    });
}

std::shared_ptr<Expression> Semantic::CreateTemporary(Type* t, Context c) {
    return CreatePrimGlobal(Range::Empty(), t->analyzer.GetLvalueType(t), [=](CodegenContext& con) {
        return con.CreateAlloca(t);
    });
}
std::shared_ptr<Expression> Semantic::CreateAddressOf(std::shared_ptr<Expression> expr, Context c) {
    return CreateResultExpression(Range::Elements(expr), [=]() {
        auto ty = expr->GetType();
        if (!IsLvalueType(ty)) 
            return Semantic::CreateErrorExpression(Wide::Memory::MakeUnique<SpecificError<AddressOfNonLvalue>>(ty->analyzer, c.where, "Attempted to take the address of a non-lvalue."));
        auto result = ty->analyzer.GetPointerType(ty->Decay());
        return CreatePrimGlobal(Range::Elements(expr), result, [=](CodegenContext& con) {
            return expr->GetValue(con);
        });
    });
}
std::shared_ptr<Expression> Semantic::CreateResultExpression(Range::Erased<std::shared_ptr<Expression>> dependents, std::function<std::shared_ptr<Expression>()> func) {
    struct FunctionalResultExpression : ResultExpression {
        FunctionalResultExpression(std::function<std::shared_ptr<Expression>()> f, Range::Erased<std::shared_ptr<Expression>> dependents)
            : ResultExpression(dependents), func(f) {}
        std::function<std::shared_ptr<Expression>()> func;
        std::shared_ptr<Expression> CalculateResult() override final {
            return func();
        }
    };
    // innit
    auto result = std::make_shared<FunctionalResultExpression>(func, dependents);
    result->GetType();
    return result;
}
std::shared_ptr<Expression> Semantic::CreatePrimGlobal(Range::Erased<std::shared_ptr<Expression>> dependents, Analyzer& a, std::function<void(CodegenContext&)> func) {
    return CreatePrimGlobal(dependents, a.GetVoidType(), [=](CodegenContext& con) {
        func(con);
        return nullptr;
    });
}
std::shared_ptr<Expression> Semantic::CreateErrorExpression(std::unique_ptr<Wide::Semantic::Error> err) {
    struct ErrorExpression : Expression {
        ErrorExpression(std::unique_ptr<Wide::Semantic::Error> e)
            : error(std::move(e)) {}
        std::unique_ptr<Wide::Semantic::Error> error;
        Type* GetType() override final {
            return nullptr;
        }
        bool IsConstantExpression() override final {
            return false;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            Wide::Util::DebugBreak();
            return nullptr;
        }
    };
    return std::make_shared<ErrorExpression>(std::move(err));
}
bool Expression::IsConstant() {
    return GetType() && GetType()->IsConstant() && IsConstantExpression();
}
