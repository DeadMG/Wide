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
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::PointerType* FunctionType::GetLLVMType(llvm::Module* module) {
    llvm::Type* ret;
    std::vector<llvm::Type*> args;
    if (ReturnType->IsComplexType()) {
        ret = analyzer.GetVoidType()->GetLLVMType(module);
        args.push_back(analyzer.GetRvalueType(ReturnType)->GetLLVMType(module));
    } else {
        ret = ReturnType->GetLLVMType(module);
    }
    for(auto&& x : Args) {
        if (x->IsComplexType()) {
            args.push_back(analyzer.GetRvalueType(x)->GetLLVMType(module));
        } else {
            args.push_back(x->GetLLVMType(module));
        }
    }
    return llvm::FunctionType::get(ret, args, variadic)->getPointerTo();
}

Wide::Util::optional<clang::QualType> FunctionType::GetClangType(ClangTU& from) {
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

std::shared_ptr<Expression> FunctionType::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    struct Call : Expression {
        Call(Analyzer& an, std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c, llvm::CallingConv::ID conv)
        : a(an), args(std::move(args)), val(std::move(self)), c(c), convention(conv)
        {
            if (GetType()->IsComplexType()) {
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
        Context c;
        
        Type* GetType() override final {
            auto fty = dynamic_cast<FunctionType*>(val->GetType());
            return fty->GetReturnType();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            llvm::Value* llvmfunc = val->GetValue(con);
            std::vector<llvm::Value*> llvmargs;
            if (GetType()->IsComplexType())
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
std::string FunctionType::explain() {
    auto begin = ReturnType->explain() + "(*)(";
    for (auto& ty : Args) {
        if (&ty != &Args.back())
            begin += ty->explain() + ", ";
        else
            begin += ty->explain();
    }
    return begin + ")";
}
std::size_t FunctionType::size() {
    return analyzer.GetDataLayout().getPointerSize();
}
std::size_t FunctionType::alignment() {
    return analyzer.GetDataLayout().getPointerABIAlignment();
}
bool FunctionType::CanThunkFromFirstToSecond(FunctionType* lhs, FunctionType* rhs, Type* context) {
    // RHS is most derived type- that is, we are building a thunk which is of type lhs, and returns a function call of type rhs.
    // Calling convention mismatch totally acceptable here.
    if (!Type::IsFirstASecond(rhs->GetReturnType(), lhs->GetReturnType(), context))
        return false;
    // The first argument may be adjusted.
    if (lhs->variadic != rhs->variadic) return false;
    if (lhs->Args.size() != rhs->Args.size()) return false;
    for (unsigned int i = 1; i < rhs->Args.size(); ++i)
        if (!Type::IsFirstASecond(lhs->Args[i], rhs->Args[i], context))
            return false;
    // For the first argument, permit adjustment.
    if (!rhs->Args.empty())
        if (rhs->Args[0]->Decay()->IsDerivedFrom(lhs->Args[0]->Decay()) == InheritanceRelationship::UnambiguouslyDerived)
            if (IsLvalueType(rhs->Args[0]) != IsLvalueType(lhs->Args[0]))
                return false;
    return true;
}
std::function<llvm::Function*(llvm::Module*)> FunctionType::CreateThunk(std::function<llvm::Function*(llvm::Module*)> from, FunctionType* source, FunctionType* dest, Type* context) {
    if (source == dest) return from;
    auto name = source->analyzer.GetUniqueFunctionName();
    auto create_body = CreateThunk(from, [name](llvm::Module* m) {
        return m->getFunction(name);
    }, source, dest, context);
    return [name, create_body, source](llvm::Module* m) {
        llvm::Function::Create(llvm::cast<llvm::FunctionType>(source->GetLLVMType(m)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, name, m);
        create_body(m);
        return m->getFunction(name);
    };
}
std::function<void(llvm::Module*)> FunctionType::CreateThunk(std::function<llvm::Function*(llvm::Module*)> from, std::function<llvm::Function*(llvm::Module*)> to, FunctionType* source, FunctionType* dest, Type* context) {
    // Emit thunk from from to to, with functiontypes source and dest.
    // Beware of ABI demons.
    std::vector<std::shared_ptr<Expression>> conversion_exprs;

    for (unsigned i = 1; i < source->Args.size(); ++i) {
        struct self : Expression {
            self(Type* t, unsigned i) : ty(t), i(i) {}
            Type* ty;
            unsigned i;
            Type* GetType() override final { return ty; }
            llvm::Value* ComputeValue(CodegenContext& con) { return std::next(con->GetInsertBlock()->getParent()->arg_begin(), i); }
        };
        conversion_exprs.push_back(dest->Args[i]->BuildValueConstruction({ std::make_shared<self>(source->Args[i], i + source->GetReturnType()->IsComplexType()) }, { context, std::make_shared<std::string>("Analyzer internal thunk") }));
    }
    return [from, to, conversion_exprs](llvm::Module* m) {
        CodegenContext::EmitFunctionBody(from(m), [to, conversion_exprs](CodegenContext& con) {
            std::vector<llvm::Value*> arguments;
            for (auto arg : conversion_exprs)
                arguments.push_back(arg->GetValue(con));

        });

    };
}