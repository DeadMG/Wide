#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Expression.h>
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
    if (ReturnType->IsComplexType(module)) {
        ret = analyzer.GetVoidType()->GetLLVMType(module);
        args.push_back(analyzer.GetRvalueType(ReturnType)->GetLLVMType(module));
    } else {
        ret = ReturnType->GetLLVMType(module);
    }
    for(auto&& x : Args) {
        if (x->IsComplexType(module)) {
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

std::unique_ptr<Expression> FunctionType::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    struct Call : Expression {
        Call(Analyzer& an, std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> args, Context c)
        : a(an), args(std::move(args)), val(std::move(self)), c(c)
        {}

        Analyzer& a;
        std::vector<std::unique_ptr<Expression>> args;
        std::unique_ptr<Expression> val;
        std::unique_ptr<Expression> Ret;
        Context c;
        
        Type* GetType() override final {
            auto fty = dynamic_cast<FunctionType*>(val->GetType());
            return fty->GetReturnType();
        }
        void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            if (Ret)
                Ret->DestroyLocals(module, bb, allocas);
            for (auto rit = args.rbegin(); rit != args.rend(); ++rit)
                (*rit)->DestroyLocals(module, bb, allocas);
            val->DestroyLocals(module, bb, allocas);
        }
        llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
            llvm::Value* llvmfunc = val->GetValue(module, bb, allocas);
            std::vector<llvm::Value*> llvmargs;
            if (GetType()->IsComplexType(module)) {
                Ret = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(GetType(), c);
                llvmargs.push_back(Ret->GetValue(module, bb, allocas));
            }
            for (auto&& arg : args)
                llvmargs.push_back(arg->GetValue(module, bb, allocas));
            auto call = bb.CreateCall(llvmfunc, llvmargs);
            if (Ret)
                return Ret->GetValue(module, bb, allocas);
            return call;
        }
    };
    return Wide::Memory::MakeUnique<Call>(analyzer, std::move(val), std::move(args), c);
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