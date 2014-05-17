#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::PointerType* FunctionType::GetLLVMType(Codegen::Generator& g) {
    llvm::Type* ret;
    std::vector<llvm::Type*> args;
    if (ReturnType->IsComplexType(g)) {
        ret = analyzer.GetVoidType()->GetLLVMType(g);
        args.push_back(analyzer.GetRvalueType(ReturnType)->GetLLVMType(g));
    } else {
        ret = ReturnType->GetLLVMType(g);
    }
    for(auto&& x : Args) {
        if (x->IsComplexType(g)) {
            args.push_back(analyzer.GetRvalueType(x)->GetLLVMType(g));
        } else {
            args.push_back(x->GetLLVMType(g));
        }
    }
    return llvm::FunctionType::get(ret, args, false)->getPointerTo();
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
    return from.GetASTContext().getFunctionType(*retty, types, clang::FunctionProtoType::ExtProtoInfo());
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
        void DestroyLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            if (Ret)
                Ret->DestroyLocals(g, bb);
            for (auto rit = args.rbegin(); rit != args.rend(); ++rit)
                (*rit)->DestroyLocals(g, bb);
            val->DestroyLocals(g, bb);
        }
        llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            llvm::Value* llvmfunc = val->GetValue(g, bb);
            std::vector<llvm::Value*> llvmargs;
            if (GetType()->IsComplexType(g)) {
                Ret = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(GetType(), c);
                llvmargs.push_back(Ret->GetValue(g, bb));
            }
            for (auto&& arg : args)
                llvmargs.push_back(arg->GetValue(g, bb));
            auto call = bb.CreateCall(llvmfunc, llvmargs);
            if (Ret)
                return Ret->GetValue(g, bb);
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