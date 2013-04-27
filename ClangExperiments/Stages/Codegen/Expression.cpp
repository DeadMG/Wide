#include "Expression.h"
#include "Generator.h"
#include "Function.h"
#include <set>

#pragma warning(push, 0)

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

using namespace Wide;
using namespace Codegen;

llvm::Value* Expression::GetValue(llvm::IRBuilder<>& bb, Generator& g){
    if (!val) val = ComputeValue(bb, g);
    return val;
}
void Expression::Build(llvm::IRBuilder<>& bb, Generator& g) {
    GetValue(bb, g);
}


FunctionCall::FunctionCall(Expression* obj, std::vector<Expression*> args, std::function<llvm::Type*(llvm::Module*)> ty)
    : object(obj), arguments(std::move(args)), CastTy(std::move(ty)) {}


llvm::Value* FunctionCall::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto&& mod = builder.GetInsertBlock()->getParent()->getParent();
    auto obj = object->GetValue(builder, g);
    auto fun = llvm::dyn_cast<llvm::Function>(obj);
    auto funty = fun->getFunctionType();
    std::set<std::size_t> ignoredparams;
    std::vector<llvm::Value*> values;    
    for(unsigned int i = 0; i < arguments.size(); ++i) {
        // Build even if the param is dropped- the expression may have side effects.
        // These arguments may be modified to deal with ABI issues.
        values.push_back(arguments[i]->GetValue(builder, g));
    }
    std::vector<unsigned> byval;
    if (CastTy) {
        auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(CastTy(mod))->getElementType());
        if (funty != t) {
            // The numerous ways in which this can occur even in well-formed code are:
        
            // i1/i8 mismatch
            // Clang return type coercion
            // Clang dropping last-parameter empty types
            // Clang using byval to confuse us
            // Clang can generate functions and then use bitcasts itself to call them (not currently covered as no test cases)
            // Clang can coerce parameters sometimes (not yet encountered)
        
            // Handle the parameters for now, check the return type after the call has been generated.            
            // Check for Clang dropping last-parameter empty types
            if (funty->getNumParams() != t->getNumParams())
                if (t->getNumParams() == funty->getNumParams() + 1)
                    if (g.IsEliminateType(t->getParamType(t->getNumParams() - 1)))
                        values.pop_back();
            if (values.size() != funty->getNumParams())
                assert(false && "The number of parameters was a mismatch that was not covered by empty-type elimination.");


            for(auto arg_begin = fun->arg_begin(); arg_begin != fun->arg_end(); ++arg_begin) {
                // If this argument is a match then great
                auto arg_ty = arg_begin->getType();
                auto param_ty = t->getParamType(arg_begin->getArgNo());
                auto argnum = arg_begin->getArgNo();
                if (arg_ty == param_ty)
                    continue;

                // Check for Clang using byval to confuse us.
                if (auto ptrty = llvm::dyn_cast<llvm::PointerType>(arg_ty)) {
                    if (ptrty->getElementType() != param_ty)
                        assert(false && "The parameter types did not match and byval could not explain the mismatch.");
                        // If byval, then ignore this mismatch.
                    if (!arg_begin->hasByValAttr())
                        assert(false && "The parameter types did not match and byval could not explain the mismatch.");

                    auto alloc = builder.CreateAlloca(param_ty);
                    alloc->setAlignment(arg_begin->getParamAlignment());
                    builder.CreateStore(values[argnum], alloc);
                    values[argnum] = alloc;
                    byval.push_back(argnum);
                } else if (arg_ty == llvm::IntegerType::getInt1Ty(mod->getContext()) && param_ty == llvm::IntegerType::getInt8Ty(mod->getContext()))  {
                    // i8-i1 mismatch, fix them up with a trunc
                    values[argnum] = builder.CreateTrunc(values[argnum], llvm::IntegerType::getInt1Ty(mod->getContext()));
                } else
                    assert(false && "The parameter types did not match and (byval) || (i1/i8) could not explain the mismatch.");
            }
        }
    }
    auto call = builder.CreateCall(obj, values);
    llvm::Value* val = call;
    // If Clang returns i1 then automatically zext it to i8 as we expect.
    if (CastTy) {
        auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(CastTy(mod))->getElementType());
        if (t->getReturnType() != val->getType()) {
            auto layout = llvm::DataLayout(mod->getDataLayout());
            if (val->getType() == llvm::IntegerType::getInt1Ty(mod->getContext()) && t->getReturnType() == llvm::IntegerType::getInt8Ty(mod->getContext()))
                val = builder.CreateZExt(val, llvm::IntegerType::getInt8Ty(mod->getContext()));
            else if (layout.getTypeSizeInBits(val->getType()) == layout.getTypeSizeInBits(t->getReturnType())) {
                // Expect random return type coercion, so coerce back
                auto alloc = builder.CreateAlloca(t->getReturnType());
                auto cast = builder.CreateBitCast(alloc, val->getType()->getPointerTo());
                builder.CreateStore(val, cast);
                val = builder.CreateLoad(alloc);
            } else
                assert(false && "The return types did not match and (i1/i8) || (return type coercion) could not explain the mismatch.");
        }
    }
    // Set the byvals with that attribute
    for(auto x : byval) {
        call->addAttribute(x + 1, llvm::Attribute::AttrKind::ByVal);
    }
    return val;
}


FunctionValue::FunctionValue(std::string name)
    : mangled_name(std::move(name)) {}

llvm::Value* FunctionValue::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.GetInsertBlock()->getParent()->getParent()->getFunction(mangled_name);
}

llvm::Value* LoadExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateLoad(obj->GetValue(builder, g), false);
}

llvm::Value* StoreExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    /*auto llvmlhs = val->GetValue(builder)->getType();
    auto llvmrhs = obj->GetValue(builder)->getType();
    auto el = llvm::dyn_cast<llvm::PointerType>(llvmrhs)->getElementType();
    el->dump();
    std::cout << "\n";
    llvmlhs->dump();*/

    builder.CreateStore(val->GetValue(builder, g), obj->GetValue(builder, g), false);  
    return obj->GetValue(builder, g);
}

llvm::Value* StringExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) { 
    return builder.CreateGlobalStringPtr(value);
}

llvm::Value* NamedGlobalVariable::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.GetInsertBlock()->getParent()->getParent()->getGlobalVariable(mangled);
}

llvm::Value* Int8Expression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.getInt8(value);
}

llvm::Value* Variable::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateAlloca(t(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* ChainExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    s->Build(builder, g);
    return next->GetValue(builder, g);
}

llvm::Value* FieldExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateStructGEP(obj->GetValue(builder, g), fieldnum);
}

llvm::Value* ParamExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto f = builder.GetInsertBlock()->getParent();
    return g.FromLLVMFunc(f)->GetParameter(param());
}

llvm::Value* TruncateExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateTrunc(val->GetValue(builder, g), ty(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* NullExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return llvm::Constant::getNullValue(ty(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* IntegralLeftShiftExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateShl(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* IntegralRightShiftExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateLShr(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}