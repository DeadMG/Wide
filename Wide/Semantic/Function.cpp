#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionSkeleton.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/UserDefinedType.h>

using namespace Wide;
using namespace Semantic;

Function::Function(Analyzer& a, FunctionSkeleton* skel, std::vector<Type*> args)
    : analyzer(a)
    , skeleton(skel)
    , Args(args) 
{

}

void Function::ComputeBody() {
    for (auto&& stmt : skeleton->ComputeBody()) {
        stmt->Instantiate(this);
    }
    if (ReturnType) return;
    auto returns = skeleton->GetReturns();
    if (!skeleton->GetExplicitReturn(Args)) {
        if (returns.size() == 0) {
            ReturnType = analyzer.GetVoidType();
            return;
        }

        std::unordered_set<Type*> ret_types;
        for (auto ret : returns) {
            if (!ret->GetReturnType(Args)) continue;
            ret_types.insert(ret->GetReturnType(Args)->Decay());
        }

        if (ret_types.size() == 1) {
            ReturnType = *ret_types.begin();
            ReturnTypeChanged(ReturnType);
            return;
        }

        // If there are multiple return types, there should be a single return type where the rest all is-a that one.
        std::unordered_set<Type*> isa_rets;
        for (auto ret : ret_types) {
            auto the_rest = ret_types;
            the_rest.erase(ret);
            auto all_isa = [&] {
                for (auto other : the_rest) {
                    if (!Type::IsFirstASecond(other, ret, this))
                        return false;
                }
                return true;
            };
            if (all_isa())
                isa_rets.insert(ret);
        }
        if (isa_rets.size() == 1) {
            ReturnType = *isa_rets.begin();
            ReturnTypeChanged(ReturnType);
            return;
        }
        throw std::runtime_error("Fuck");
    } else {
        ReturnType = skeleton->GetExplicitReturn(Args);
    }
}
llvm::Function* Function::EmitCode(llvm::Module* module) {
    if (llvmfunc) {
        if (llvmfunc->getParent() == module)
            return llvmfunc;
        return module->getFunction(llvmfunc->getName());
    }
    auto sig = GetSignature();
    auto llvmsig = sig->GetLLVMType(module);
    if (import_name) {
        if (llvmfunc = module->getFunction(*import_name))
            return llvmfunc;
        llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, *import_name, module);
        for (auto exportnam : trampoline)
            exportnam(module);
        return llvmfunc;
    }
    llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, llvmname, module);
    CodegenContext::EmitFunctionBody(llvmfunc, [this](CodegenContext& c) {
        for (auto&& stmt : skeleton->ComputeBody())
            if (!c.IsTerminated(c->GetInsertBlock()))
                stmt->GenerateCode(c);

        if (!c.IsTerminated(c->GetInsertBlock())) {
            if (ReturnType == analyzer.GetVoidType()) {
                c.DestroyAll(false);
                c->CreateRetVoid();
            } else
                c->CreateUnreachable();
        }
    });

    for (auto exportnam : trampoline)
        exportnam(module);
    return llvmfunc;
}

std::vector<std::shared_ptr<Expression>> Function::AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
    // May need to perform conversion on "this" that isn't handled by the usual machinery.
    // But check first, because e.g. Derived& to Base& is fine.
    if (analyzer.HasImplicitThis(skeleton->GetASTFunction(), skeleton->GetContext()) && !Type::IsFirstASecond(args[0]->GetType(key), Args[0], c.from)) {
        auto argty = args[0]->GetType(key);
        // If T&&, cast.
        // Else, build a T&& from the U then cast that. Use this instead of BuildRvalueConstruction because we may need to preserve derived semantics.
        if (argty == analyzer.GetRvalueType(skeleton->GetNonstaticMemberContext())) {
            args[0] = std::make_shared<LvalueCast>(args[0]);
        } else if (argty != analyzer.GetLvalueType(skeleton->GetNonstaticMemberContext())) {
            args[0] = std::make_shared<LvalueCast>(analyzer.GetRvalueType(skeleton->GetNonstaticMemberContext())->BuildValueConstruction({ args[0] }, c));
        }
    }
    return AdjustArgumentsForTypes(key, std::move(args), Args, c);
}
WideFunctionType* Function::GetSignature() {
    ComputeBody();
    return analyzer.GetFunctionType(ReturnType, Args, false);
}
std::shared_ptr<Expression> Function::CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) {
    ComputeBody();

    // Figure out if we need to do a dynamic dispatch.
    return CreatePrimGlobal(GetSignature(), [=](CodegenContext& con) -> llvm::Value* {
        if (!llvmfunc)
            EmitCode(con);

        if (args.empty())
            return llvmfunc;

        auto func = dynamic_cast<const Parse::DynamicFunction*>(skeleton->GetASTFunction());
        auto udt = dynamic_cast<UserDefinedType*>(args[0]->GetType(Args)->Decay());
        if (!func || !udt) return llvmfunc;
        auto obj = Type::GetVirtualPointer(args[0]);
        auto vindex = udt->GetVirtualFunctionIndex(func);
        if (!vindex) return llvmfunc;
        auto vptr = con->CreateLoad(obj->GetValue(con));
        return con->CreatePointerCast(con->CreateLoad(con->CreateConstGEP1_32(vptr, *vindex)), GetSignature()->GetLLVMType(con));
    });
}
