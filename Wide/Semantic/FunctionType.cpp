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
bool FunctionType::IsCompatible(FunctionType* first, FunctionType* second) {
    return false;
}