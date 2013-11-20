#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/LLVMGenerator.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Codegen/Function.h>
#include <set>
#include <array>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace LLVMCodegen;

llvm::Value* Expression::GetValue(llvm::IRBuilder<>& bb, Generator& g){
    if (!val) 
        val = ComputeValue(bb, g);
    return val;
}
void Expression::Build(llvm::IRBuilder<>& bb, Generator& g) {
    GetValue(bb, g);
}

FunctionCall::FunctionCall(LLVMCodegen::Expression* obj, std::vector<LLVMCodegen::Expression*> args, std::function<llvm::Type*(llvm::Module*)> ty)
    : arguments(std::move(args)), object(obj), CastTy(std::move(ty)) {}

llvm::Value* FunctionCall::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto&& mod = builder.GetInsertBlock()->getParent()->getParent();
    auto obj = object->GetValue(builder, g);
    if (!obj) {
        if (auto fun = dynamic_cast<FunctionValue*>(object)) {
            auto f = g.GetFunctionByName(fun->GetMangledName());
            f->Declare(mod, builder.getContext(), g);
            obj = object->GetValue(builder, g);
        }
    }
    if (!obj)
        Wide::Util::DebugBreak();
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

llvm::Value* IntegralExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto ty = llvm::dyn_cast<llvm::IntegerType>(type(builder.GetInsertBlock()->getParent()->getParent()));
    return llvm::ConstantInt::get(ty, value, !sign);
}


llvm::Value* Variable::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto alloc = builder.CreateAlloca(t(builder.GetInsertBlock()->getParent()->getParent()));
    alloc->setAlignment(align);
    return alloc;
}

llvm::Value* ChainExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    s->Build(builder, g);
    
    return next->GetValue(builder, g);
}

llvm::Value* FieldExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto val = obj->GetValue(builder, g);
    if (val->getType()->isPointerTy())
        return builder.CreateStructGEP(obj->GetValue(builder, g), fieldnum());
    else {
        std::vector<uint32_t> args;
        args.push_back(fieldnum());
        return builder.CreateExtractValue(val, args);
    }
}

llvm::Value* ParamExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto f = builder.GetInsertBlock()->getParent();
    return g.FromLLVMFunc(f)->GetParameter(param());
}

llvm::Value* TruncateExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto truncty = ty(builder.GetInsertBlock()->getParent()->getParent());
    if (truncty->isFloatingPointTy())
        return builder.CreateFPTrunc(val->GetValue(builder, g), truncty);
    else
        return builder.CreateTrunc(val->GetValue(builder, g), truncty);
}

llvm::Value* NullExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return llvm::Constant::getNullValue(ty(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* IntegralLeftShiftExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateShl(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* IntegralRightShiftExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    if (is_signed)
        return builder.CreateAShr(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
    else
        return builder.CreateLShr(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* IntegralLessThan::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    // Semantic will expect an i8 here, not an i1.
    auto cmp = sign ? llvm::CmpInst::Predicate::ICMP_SLT : llvm::CmpInst::Predicate::ICMP_ULT;
    return builder.CreateZExt(builder.CreateICmp(cmp, lhs->GetValue(builder, g), rhs->GetValue(builder, g)), llvm::IntegerType::getInt8Ty(builder.getContext()));
}

llvm::Value* ZExt::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateZExt(from->GetValue(builder, g), to(builder.GetInsertBlock()->getParent()->getParent()));
}
llvm::Value* SExt::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateSExt(from->GetValue(builder, g), to(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* NegateExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto obj = expr->GetValue(builder, g);
    return builder.CreateNot(obj);
}

llvm::Value* OrExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateOr(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* EqualityExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto lhsval = lhs->GetValue(builder, g);
    auto rhsval = rhs->GetValue(builder, g);
    if (lhsval->getType()->isFloatingPointTy())
        return builder.CreateFCmpOEQ(lhsval, rhsval);
    else
        return builder.CreateICmpEQ(lhsval, rhsval);
}

llvm::Value* PlusExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto lhsval = lhs->GetValue(builder, g);
    auto rhsval = rhs->GetValue(builder, g);
    if (lhsval->getType()->isFloatingPointTy())
        return builder.CreateFAdd(lhsval, rhsval);
    else
        return builder.CreateAdd(lhsval, rhsval);
}

llvm::Value* MultiplyExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto lhsval = lhs->GetValue(builder, g);
    auto rhsval = rhs->GetValue(builder, g);
    if (lhsval->getType()->isFloatingPointTy())
        return builder.CreateFMul(lhsval, rhsval);
    else
        return builder.CreateMul(lhsval, rhsval);
}

llvm::Value* AndExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateAnd(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* IsNullExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateZExt(builder.CreateIsNull(ptr->GetValue(builder, g)), llvm::IntegerType::getInt8Ty(builder.getContext()));
}

llvm::Value* XorExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateXor(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* SubExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto lhsval = lhs->GetValue(builder, g);
    auto rhsval = rhs->GetValue(builder, g);
    if (lhsval->getType()->isFloatingPointTy())
        return builder.CreateFSub(lhsval, rhsval);
    else
        return builder.CreateSub(lhsval, rhsval);
}

llvm::Value* DivExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    if (is_signed)
        return builder.CreateSDiv(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
    return builder.CreateUDiv(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* ModExpression::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    if (is_signed)
        return builder.CreateSRem(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
    return builder.CreateURem(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* FPExtension::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateFPExt(from->GetValue(builder, g), to(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* FPDiv::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateFDiv(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* FPMod::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateFRem(lhs->GetValue(builder, g), rhs->GetValue(builder, g));
}

llvm::Value* FPLT::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    auto lhsval = lhs->GetValue(builder, g);
    auto rhsval = rhs->GetValue(builder, g);
    return builder.CreateFCmpOLT(lhsval, rhsval);
}
llvm::Value* Nop::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    // Must create at least 1 actual instruction.
    return builder.CreateAlloca(llvm::IntegerType::getInt1Ty(builder.getContext()), nullptr, "nop");
}
llvm::Value* DeferredExpr::ComputeValue(llvm::IRBuilder<>& b, Generator& g) {
    auto expr = dynamic_cast<LLVMCodegen::Expression*>(func());
    return expr->GetValue(b, g);
}
