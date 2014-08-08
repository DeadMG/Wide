#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <clang/CodeGen/CGFunctionInfo.h>
#include <CodeGen/CodeGenFunction.h>
#include <CodeGen/CGCXXABI.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

// First, FunctionType.
std::size_t FunctionType::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t FunctionType::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
std::string FunctionType::explain() {
    auto begin = GetReturnType()->explain() + "(*)(";
    auto Args = GetArguments();
    for (auto& ty : Args) {
        if (&ty != &Args.back())
            begin += ty->explain() + ", ";
        else
            begin += ty->explain();
    }
    return begin + ")";
}
bool FunctionType::CanThunkFromFirstToSecond(FunctionType* lhs, FunctionType* rhs, Type* context, bool adjust) {
    // RHS is most derived type- that is, we are building a thunk which is of type lhs, and returns a function call of type rhs.
    // Calling convention mismatch totally acceptable here.
    if (!Type::IsFirstASecond(rhs->GetReturnType(), lhs->GetReturnType(), context))
        return false;
    // The first argument may be adjusted.
    if (lhs->GetArguments().size() != rhs->GetArguments().size()) return false;
    for (unsigned int i = adjust; i < rhs->GetArguments().size(); ++i)
        if (!Type::IsFirstASecond(lhs->GetArguments()[i], rhs->GetArguments()[i], context))
            return false;
    // For the first argument, permit adjustment.
    if (adjust)
        if (IsLvalueType(rhs->GetArguments()[0]) != IsLvalueType(lhs->GetArguments()[0]))
            return false;
    return true;
}
std::shared_ptr<Expression> FunctionType::CreateThunk(std::shared_ptr<Expression> to, WideFunctionType* dest, Type* context) {
    if (to->GetType() == dest) return to;
    auto name = dest->analyzer.GetUniqueFunctionName();
    auto functy = dynamic_cast<FunctionType*>(to->GetType());
    if (!functy) throw std::runtime_error("Cannot thunk from a non-function-type.");
    auto func = [name](llvm::Module* mod) { return mod->getFunction(name); };
    auto emit = dest->CreateThunk(func, to, name, context);
    struct self : Expression {
        self(std::function<void(llvm::Module*)> emit, WideFunctionType* dest, std::string name)
            : emit(emit), dest(dest), name(name) {}
        std::function<void(llvm::Module*)> emit;
        WideFunctionType* dest;
        std::string name;
        Type* GetType() override final { return dest; }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto func = llvm::Function::Create(llvm::cast<llvm::FunctionType>(dest->GetLLVMType(con)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, name, con);
            emit(con);
            return func;
        }
    };
    return std::make_shared<self>(emit, dest, name);
}

// Now WideFunctionType
llvm::PointerType* WideFunctionType::GetLLVMType(llvm::Module* module) {
    llvm::Type* ret;
    std::vector<llvm::Type*> args;
    if (ReturnType->AlwaysKeepInMemory()) {
        ret = analyzer.GetVoidType()->GetLLVMType(module);
        args.push_back(analyzer.GetRvalueType(ReturnType)->GetLLVMType(module));
    } else {
        ret = ReturnType->GetLLVMType(module);
    }
    for(auto&& x : Args) {
        if (x->AlwaysKeepInMemory()) {
            args.push_back(analyzer.GetRvalueType(x)->GetLLVMType(module));
        } else {
            args.push_back(x->GetLLVMType(module));
        }
    }
    return llvm::FunctionType::get(ret, args, variadic)->getPointerTo();
}

std::shared_ptr<Expression> WideFunctionType::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    struct Call : Expression {
        Call(Analyzer& an, std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c, llvm::CallingConv::ID conv)
            : a(an), args(std::move(args)), val(std::move(self)), convention(conv)
        {
            if (GetType()->AlwaysKeepInMemory()) {
                Ret = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(GetType(), c);
                if (!GetType()->IsTriviallyDestructible())
                    Destructor = GetType()->BuildDestructorCall(Ret, c, true);
                else
                    Destructor = {};
            }
        }

        Analyzer& a;
        std::vector<std::shared_ptr<Expression>> args;
        std::shared_ptr<Expression> val;
        std::shared_ptr<Expression> Ret;
        std::function<void(CodegenContext&)> Destructor;
        llvm::CallingConv::ID convention;

        Type* GetType() override final {
            auto fty = dynamic_cast<FunctionType*>(val->GetType());
            return fty->GetReturnType();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            llvm::Value* llvmfunc = val->GetValue(con);
            std::vector<llvm::Value*> llvmargs;
            if (GetType()->AlwaysKeepInMemory())
                llvmargs.push_back(Ret->GetValue(con));
            // The CALLER calls the destructor, NOT the CALLEE. So let this just destroy them naturally.
            for (auto&& arg : args)
                llvmargs.push_back(arg->GetValue(con));
            llvm::Value* call;
            // We need to invoke if we're not destructing, and we have something to destroy OR a catch block we may need to jump to.
            if (!con.destructing && (con.HasDestructors() || con.EHHandler)) {
                llvm::BasicBlock* continueblock = llvm::BasicBlock::Create(con, "continue", con->GetInsertBlock()->getParent());
                // If we have a try/catch block, let the catch block figure out what to do.
                // Else, kill everything in the scope and resume.
                auto invokeinst = con->CreateInvoke(llvmfunc, continueblock, con.CreateLandingpadForEH(), llvmargs);
                con->SetInsertPoint(continueblock);
                invokeinst->setCallingConv(convention);
                call = invokeinst;
            } else {
                auto callinst = con->CreateCall(llvmfunc, llvmargs);
                callinst->setCallingConv(convention);
                call = callinst;
            }
            if (Ret) {
                if (Destructor)
                    con.AddDestructor(Destructor);
                return Ret->GetValue(con);
            }
            return call;
        }
    };
    return Wide::Memory::MakeUnique<Call>(analyzer, std::move(val), std::move(args), c, convention);
}
Type* WideFunctionType::GetReturnType() {
    return ReturnType;
}
std::vector<Type*> WideFunctionType::GetArguments() {
    return Args;
}
Wide::Util::optional<clang::QualType> WideFunctionType::GetClangType(ClangTU& from) {
    std::vector<clang::QualType> types;
    for (auto x : Args) {
        auto clangty = x->GetClangType(from);
        if (!clangty) return Wide::Util::none;
        types.push_back(*clangty);
    }
    auto retty = ReturnType->GetClangType(from);
    if (!retty) return Wide::Util::none;
    clang::FunctionProtoType::ExtProtoInfo protoinfo;
    protoinfo.Variadic = variadic;
    std::map<llvm::CallingConv::ID, clang::CallingConv> convconverter = {
        { llvm::CallingConv::C, clang::CallingConv::CC_C },
        { llvm::CallingConv::X86_StdCall, clang::CallingConv::CC_X86StdCall },
        { llvm::CallingConv::X86_FastCall, clang::CallingConv::CC_X86FastCall },
        { llvm::CallingConv::X86_ThisCall, clang::CallingConv::CC_X86ThisCall },
        //{ clang::CallingConv::CC_X86Pascal, },
        { llvm::CallingConv::X86_64_Win64, clang::CallingConv::CC_X86_64Win64 },
        { llvm::CallingConv::X86_64_SysV, clang::CallingConv::CC_X86_64SysV },
        { llvm::CallingConv::ARM_AAPCS, clang::CallingConv::CC_AAPCS },
        { llvm::CallingConv::ARM_AAPCS_VFP, clang::CallingConv::CC_AAPCS_VFP },
        //{ clang::CallingConv::CC_PnaclCall, },
        { llvm::CallingConv::Intel_OCL_BI, clang::CallingConv::CC_IntelOclBicc },
    };
    protoinfo.ExtInfo = protoinfo.ExtInfo.withCallingConv(convconverter.at(convention));
    return from.GetASTContext().getFunctionType(*retty, types, protoinfo);
}
std::function<void(llvm::Module*)> WideFunctionType::CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, std::string name, Type* context) {
    std::vector<std::shared_ptr<Expression>> conversion_exprs;
    auto destty = dynamic_cast<WideFunctionType*>(dest->GetType());
    assert(destty);
    assert(destty != this);
    Context c{ context, std::make_shared<std::string>("Analyzer internal thunk") };
    // For the zeroth argument, if the rhs is derived from the lhs, force a cast for vthunks.
    struct arg : Expression {
        arg(Type* t, unsigned i) : ty(t), i(i) {}
        Type* ty;
        unsigned i;
        Type* GetType() override final { return ty; }
        llvm::Value* ComputeValue(CodegenContext& con) { 
            return std::next(con->GetInsertBlock()->getParent()->arg_begin(), i); 
        }
    };
    for (unsigned i = 0; i < GetArguments().size(); ++i) {
        if (i == 0) {
            auto derthis = destty->GetArguments()[0]->Decay();
            auto basethis = GetArguments()[0]->Decay();
            if (derthis->IsDerivedFrom(basethis) == InheritanceRelationship::UnambiguouslyDerived && IsLvalueType(derthis) == IsLvalueType(basethis)) {
                struct cast : Expression {
                    cast(Type* dest, unsigned off, bool complex)
                        : desttype(dest), offset(off), complexthis(complex) {}
                    Type* desttype;
                    unsigned offset;
                    bool complexthis;
                    Type* GetType() override final { return desttype; }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        auto src = std::next(con->GetInsertBlock()->getParent()->arg_begin(), complexthis);
                        auto cast = con->CreateBitCast(src, con.GetInt8PtrTy());
                        auto adjusted = con->CreateGEP(cast, llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(con), -offset, true));
                        return con->CreateBitCast(adjusted, desttype->GetLLVMType(con));
                    }
                };
                conversion_exprs.push_back(std::make_shared<cast>(destty->GetArguments()[0], derthis->GetOffsetToBase(basethis), GetReturnType()->AlwaysKeepInMemory()));
                continue;
            }
        }
        conversion_exprs.push_back(destty->GetArguments()[i]->BuildValueConstruction({ std::make_shared<arg>(GetArguments()[i], i + GetReturnType()->AlwaysKeepInMemory()) }, c));
    }
    auto call = destty->BuildCall(dest, conversion_exprs, c);
    std::shared_ptr<Expression> ret_expr;
    if (GetReturnType()->AlwaysKeepInMemory()) {
        ret_expr = GetReturnType()->BuildInplaceConstruction(std::make_shared<arg>(analyzer.GetLvalueType(GetReturnType()), 0), { call }, c);
    } else if (GetReturnType() != analyzer.GetVoidType())
        ret_expr = GetReturnType()->BuildValueConstruction({ call }, c);
    else
        ret_expr = call;
    return [src, ret_expr](llvm::Module* mod) {
        CodegenContext::EmitFunctionBody(src(mod), [ret_expr](CodegenContext& con) {
            auto val = ret_expr->GetValue(con);
            con.DestroyAll(false);
            if (val->getType() == llvm::Type::getVoidTy(con))
                con->CreateRetVoid();
            else
                con->CreateRet(val);
        });
    };
}

// Now ClangFunctionType.
const clang::CodeGen::CGFunctionInfo& ClangFunctionType::GetCGFunctionInfo(llvm::Module* module) {
   return from->GetABIForFunction(type, self ? self->getNonReferenceType()->getAsCXXRecordDecl() : nullptr, module);
}
llvm::PointerType* ClangFunctionType::GetLLVMType(llvm::Module* module) {
    return from->GetFunctionPointerType(GetCGFunctionInfo(module), module);
}
Wide::Util::optional<clang::QualType> ClangFunctionType::GetClangType(ClangTU& to) {
    if (&to != from) return Util::none;
    if (self) return Util::none;
    return from->GetASTContext().getFunctionType(type->getResultType(), type->getArgTypes(), type->getExtProtoInfo());
}
Type* ClangFunctionType::GetReturnType() {
    return analyzer.GetClangType(*from, type->getResultType());
}
std::vector<Type*> ClangFunctionType::GetArguments() {
    std::vector<Type*> out;
    if (self)
        out.push_back(analyzer.GetClangType(*from, *self));
    for (auto arg : type->getArgTypes())
        out.push_back(analyzer.GetClangType(*from, arg));
    return out;
}

// Now the thunks.
std::shared_ptr<Expression> ClangFunctionType::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    struct Call : Expression {
        Call(Analyzer& an, std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c)
            : a(an), args(std::move(args)), val(std::move(self))
        {
            if (GetType()->AlwaysKeepInMemory()) {
                Ret = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(GetType(), c);
                if (!GetType()->IsTriviallyDestructible())
                    Destructor = GetType()->BuildDestructorCall(Ret, c, true);
                else
                    Destructor = {};
            }
        }
        Type* GetType() override final {
            auto fty = dynamic_cast<ClangFunctionType*>(val->GetType());
            return fty->GetReturnType();
        }

        Analyzer& a;
        std::vector<std::shared_ptr<Expression>> args;
        std::shared_ptr<Expression> val;
        std::shared_ptr<Expression> Ret;
        std::function<void(CodegenContext&)> Destructor;
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto clangfuncty = dynamic_cast<ClangFunctionType*>(val->GetType());
            llvm::Value* llvmfunc = val->GetValue(con);
            clang::CodeGen::CodeGenFunction codegenfunc(clangfuncty->from->GetCodegenModule(con), true);
            codegenfunc.AllocaInsertPt = con.GetAllocaInsertPoint();
            codegenfunc.Builder.SetInsertPoint(con->GetInsertBlock(), con->GetInsertBlock()->end());
            clang::CodeGen::CallArgList list;
            for (auto&& arg : args) {
                auto val = arg->GetValue(con); 
                if (arg->GetType() == a.GetBooleanType())
                    val = con->CreateTrunc(val, llvm::IntegerType::getInt1Ty(con));
                auto clangty = *arg->GetType()->GetClangType(*clangfuncty->from);
                if (arg->GetType()->AlwaysKeepInMemory())
                    list.add(clang::CodeGen::RValue::getAggregate(val), clangty);
                else
                    list.add(clang::CodeGen::RValue::get(val), clangty);
            }
            llvm::Instruction* call_or_invoke;
            clang::CodeGen::ReturnValueSlot slot;
            if (clangfuncty->GetReturnType()->AlwaysKeepInMemory()) {
                slot = clang::CodeGen::ReturnValueSlot(Ret->GetValue(con), false);
            }
            auto result = codegenfunc.EmitCall(clangfuncty->GetCGFunctionInfo(con), llvmfunc, slot, list, nullptr, &call_or_invoke);
            // We need to invoke if we're not destructing, and we have something to destroy OR a catch block we may need to jump to.
            if (!con.destructing && (con.HasDestructors() || con.EHHandler)) {
                llvm::BasicBlock* continueblock = llvm::BasicBlock::Create(con, "continue", con->GetInsertBlock()->getParent());
                // If we have a try/catch block, let the catch block figure out what to do.
                // Else, kill everything in the scope and resume.
                if (auto invokeinst = llvm::dyn_cast<llvm::InvokeInst>(call_or_invoke)) {
                    invokeinst->setUnwindDest(con.CreateLandingpadForEH());
                } else {
                    auto callinst = llvm::cast<llvm::CallInst>(call_or_invoke);
                    std::vector<llvm::Value*> args;
                    for (unsigned i = 0; i < callinst->getNumArgOperands(); ++i)
                        args.push_back(callinst->getArgOperand(i));
                    invokeinst = con->CreateInvoke(llvmfunc, continueblock, con.CreateLandingpadForEH(), args);
                    invokeinst->setAttributes(callinst->getAttributes());
                    call_or_invoke = invokeinst;
                    callinst->eraseFromParent();
                }
                con->SetInsertPoint(continueblock);
            }

            if (!clangfuncty->GetReturnType()->IsTriviallyDestructible())
                con.AddDestructor(Destructor);
            if (clangfuncty->GetReturnType() == a.GetVoidType())
                return call_or_invoke;
            if (result.isScalar()) {
                auto val = result.getScalarVal();
                if (val->getType() == llvm::IntegerType::getInt1Ty(con))
                    return con->CreateZExt(val, llvm::IntegerType::getInt8Ty(con));
                return val;
            }
            auto val = result.getAggregateAddr();
            if (clangfuncty->GetReturnType()->IsReference() || clangfuncty->GetReturnType()->AlwaysKeepInMemory())
                return val;
            return con->CreateLoad(val);
        }
    };
    return Wide::Memory::MakeUnique<Call>(analyzer, std::move(val), std::move(args), c);
}
std::function<void(llvm::Module*)> ClangFunctionType::CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, clang::FunctionDecl* decl, Type* context) {
    // Emit thunk from from to to, with functiontypes source and dest.
    // Beware of ABI demons.
    std::vector<std::shared_ptr<Expression>> conversion_exprs;
    auto destty = dynamic_cast<WideFunctionType*>(dest->GetType());
    assert(destty);
    Context c{ context, std::make_shared<std::string>("Analyzer internal thunk") };
    // For the zeroth argument, if the rhs is derived from the lhs, force a cast for vthunks.
    auto args = std::make_shared<std::vector<llvm::Value*>>();
    struct arg : Expression {
        arg(Type* t, unsigned i, std::shared_ptr<std::vector<llvm::Value*>> vec) : arg_vec(vec), ty(t), i(i) {}
        Type* ty;
        unsigned i;
        std::shared_ptr<std::vector<llvm::Value*>> arg_vec;
        Type* GetType() override final { return ty; }
        llvm::Value* ComputeValue(CodegenContext& con) {
            auto val = arg_vec->at(i);
            if (val->getType() == llvm::IntegerType::getInt1Ty(con))
                val = con->CreateZExt(val, llvm::IntegerType::getInt8Ty(con));
            if (val->getType() == GetType()->GetLLVMType(con)->getPointerTo())
                return con->CreateLoad(val);
            return val;
        }
    };
    for (unsigned i = 0; i < GetArguments().size(); ++i) {
        if (i == 0) {
            auto derthis = destty->GetArguments()[0]->Decay();
            auto basethis = GetArguments()[0]->Decay();
            if (derthis->IsDerivedFrom(basethis) == InheritanceRelationship::UnambiguouslyDerived && IsLvalueType(derthis) == IsLvalueType(basethis)) {
                struct cast : Expression {
                    cast(Type* dest, unsigned off, bool complex, std::shared_ptr<std::vector<llvm::Value*>> arg_vec)
                        : desttype(dest), offset(off), complexret(complex), arg_vec(arg_vec) {}
                    Type* desttype;
                    unsigned offset;
                    bool complexret;
                    std::shared_ptr<std::vector<llvm::Value*>> arg_vec;
                    Type* GetType() override final { return desttype; }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        auto src = arg_vec->at(complexret);
                        auto cast = con->CreateBitCast(src, con.GetInt8PtrTy());
                        auto adjusted = con->CreateGEP(cast, llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(con), offset, true));
                        return con->CreateBitCast(adjusted, desttype->GetLLVMType(con));
                    }
                };
                conversion_exprs.push_back(std::make_shared<cast>(destty->GetArguments()[0], derthis->GetOffsetToBase(basethis), GetReturnType()->AlwaysKeepInMemory(), args));
                continue;
            }
        }
        conversion_exprs.push_back(destty->GetArguments()[i]->BuildValueConstruction({ std::make_shared<arg>(GetArguments()[i], i + GetReturnType()->AlwaysKeepInMemory(), args) }, c));
    }
    auto call = destty->BuildCall(dest, conversion_exprs, c);
    std::shared_ptr<Expression> ret_expr;
    if (GetReturnType()->AlwaysKeepInMemory()) {
        ret_expr = GetReturnType()->BuildInplaceConstruction(std::make_shared<arg>(analyzer.GetLvalueType(GetReturnType()), 0, args), { call }, c);
    } else if (GetReturnType() != analyzer.GetVoidType())
        ret_expr = GetReturnType()->BuildValueConstruction({ call }, c);
    else
        ret_expr = call;
    return [src, ret_expr, decl, this, args](llvm::Module* mod) {
        auto func = src(mod);
        CodegenContext::EmitFunctionBody(func, [ret_expr, func, decl, args, this](CodegenContext& con) {
            clang::CodeGen::CodeGenFunction codegenfunc(from->GetCodegenModule(con), true);
            codegenfunc.AllocaInsertPt = con.GetAllocaInsertPoint();
            codegenfunc.Builder.SetInsertPoint(con->GetInsertBlock(), con->GetInsertBlock()->end());
            codegenfunc.CurCodeDecl = decl;
            codegenfunc.CurGD = decl;
            clang::CodeGen::FunctionArgList list;
            if (llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                from->GetCodegenModule(con).getCXXABI().BuildInstanceFunctionParams(codegenfunc, type->getResultType(), list);               
            }
            for (auto param = decl->param_begin(); param != decl->param_end(); ++param)
                list.push_back(*param);
            codegenfunc.EmitFunctionProlog(GetCGFunctionInfo(con), func, list);
            if (llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                from->GetCodegenModule(con).getCXXABI().EmitInstanceFunctionProlog(codegenfunc);
                args->push_back(std::next(func->arg_begin(), GetReturnType()->AlwaysKeepInMemory()));
            }
            for (auto param = decl->param_begin(); param != decl->param_end(); ++param)
                args->push_back(codegenfunc.GetAddrOfLocalVar(*param));
            if (GetReturnType()->AlwaysKeepInMemory())
                args->insert(args->begin(), func->arg_begin());

            auto val = ret_expr->GetValue(con);
            con.DestroyAll(false);
            if (val->getType() == llvm::Type::getVoidTy(con))
                con->CreateRetVoid();
            else if (val->getType() == llvm::Type::getInt8Ty(con) && func->getReturnType() == llvm::Type::getInt1Ty(con))
                con->CreateRet(con->CreateTrunc(val, llvm::IntegerType::getInt1Ty(con)));
            else
                con->CreateRet(val);
        });
    };
}
std::function<void(llvm::Module*)> ClangFunctionType::CreateThunk(std::function<llvm::Function*(llvm::Module*)> src, std::shared_ptr<Expression> dest, std::string name, Type* context) {
    auto qualty = from->GetASTContext().getFunctionType(type->getResultType(), type->getArgTypes(), type->getExtProtoInfo());
    clang::FunctionDecl* decl;
    auto GetParmVarDecls = [this](std::vector<clang::QualType> types, ClangTU& TU, clang::DeclContext* recdecl) {
        std::vector<clang::ParmVarDecl*> parms;
        for (auto qualty : types) {
            parms.push_back(clang::ParmVarDecl::Create(
                TU.GetASTContext(),
                recdecl,
                clang::SourceLocation(),
                clang::SourceLocation(),
                nullptr,
                qualty,
                TU.GetASTContext().getTrivialTypeSourceInfo(qualty),
                clang::VarDecl::StorageClass::SC_None,
                nullptr
            ));
        }
        return parms;
    };
    if (self) {
        auto recdecl = (*self).getNonReferenceType()->getAsCXXRecordDecl();
        decl = clang::CXXMethodDecl::Create(
            from->GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::DeclarationNameInfo(from->GetIdentifierInfo(name), clang::SourceLocation()),
            qualty,
            from->GetASTContext().getTrivialTypeSourceInfo(qualty),
            clang::FunctionDecl::StorageClass::SC_Auto,
            false,
            false,
            clang::SourceLocation()
        );
        auto explicitparams = GetParmVarDecls(type->getArgTypes(), *from, recdecl);
        decl->setParams(explicitparams);
    } else {
        decl = clang::FunctionDecl::Create(
            from->GetASTContext(),
            from->GetDeclContext(),
            clang::SourceLocation(),
            clang::DeclarationNameInfo(from->GetIdentifierInfo(name), clang::SourceLocation()),
            qualty,
            from->GetASTContext().getTrivialTypeSourceInfo(qualty),
            clang::FunctionDecl::StorageClass::SC_Static,
            false,
            false
        );
        decl->setParams(GetParmVarDecls(type->getArgTypes(), *from, from->GetDeclContext()));
    }
    return CreateThunk(src, dest, decl, context);
}