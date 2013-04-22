#include "Expression.h"

#pragma warning(push, 0)

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

using namespace Wide;
using namespace Codegen;

llvm::Value* Expression::GetValue(llvm::IRBuilder<>& bb){
    if (!val) val = ComputeValue(bb);
    return val;
}
void Expression::Build(llvm::IRBuilder<>& bb) {
    GetValue(bb);
}


FunctionCall::FunctionCall(Expression* obj, std::vector<Expression*> args, std::function<llvm::Type*(llvm::Module*)> ty)
    : object(obj), arguments(std::move(args)), CastTy(std::move(ty)) {}


llvm::Value* FunctionCall::ComputeValue(llvm::IRBuilder<>& builder) {
    auto obj = object->GetValue(builder);
    // May have to insert bitcast because fuck Clang.
    auto funty = llvm::dyn_cast<llvm::Function>(obj)->getFunctionType();
    if (CastTy) {
        auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(CastTy(builder.GetInsertBlock()->getParent()->getParent()))->getElementType());
        if (funty != t) {
            obj = builder.CreateBitCast(obj, CastTy(builder.GetInsertBlock()->getParent()->getParent()));
            funty = t;
        }
    }
    std::vector<llvm::Value*> values;
    for(unsigned int i = 0; i < arguments.size(); ++i) {
        auto val = arguments[i]->GetValue(builder);
        auto argty = val->getType();
        auto paramty = funty->getParamType(i);
        if (argty != paramty) {
            __debugbreak();
        }
        values.push_back(val);
    }

    return builder.CreateCall(obj, values);
}


FunctionValue::FunctionValue(std::string name)
    : mangled_name(std::move(name)) {}

llvm::Value* FunctionValue::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.GetInsertBlock()->getParent()->getParent()->getFunction(mangled_name);
}

llvm::Value* LoadExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateLoad(obj->GetValue(builder), false);
}

llvm::Value* StoreExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    /*auto llvmlhs = val->GetValue(builder)->getType();
    auto llvmrhs = obj->GetValue(builder)->getType();
    auto el = llvm::dyn_cast<llvm::PointerType>(llvmrhs)->getElementType();
    el->dump();
    std::cout << "\n";
    llvmlhs->dump();*/

    builder.CreateStore(val->GetValue(builder), obj->GetValue(builder), false);  
    return obj->GetValue(builder);
}

llvm::Value* StringExpression::ComputeValue(llvm::IRBuilder<>& builder) { 
    return builder.CreateGlobalStringPtr(value);
}

llvm::Value* NamedGlobalVariable::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.GetInsertBlock()->getParent()->getParent()->getGlobalVariable(mangled);
}

llvm::Value* Int8Expression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.getInt8(value);
}

llvm::Value* Variable::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateAlloca(t(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* ChainExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    s->Build(builder);
    return next->GetValue(builder);
}

llvm::Value* FieldExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateStructGEP(obj->GetValue(builder), fieldnum);
}

llvm::Value* ParamExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    auto f = builder.GetInsertBlock()->getParent();
    auto param_num = param();
    if (param_num >= f->arg_size())
        throw std::runtime_error("Attempted to access a parameter beyond the bounds.");
    auto curr = f->arg_begin();
    std::advance(curr, param_num);
    return curr;
}

llvm::Value* TruncateExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateTrunc(val->GetValue(builder), ty(builder.GetInsertBlock()->getParent()->getParent()));
}

llvm::Value* NullExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return llvm::Constant::getNullValue(ty(builder.GetInsertBlock()->getParent()->getParent()));
}
llvm::Value* IntegralLeftShiftExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateShl(lhs->GetValue(builder), rhs->GetValue(builder));
}

llvm::Value* IntegralRightShiftExpression::ComputeValue(llvm::IRBuilder<>& builder) {
    return builder.CreateLShr(lhs->GetValue(builder), rhs->GetValue(builder));
}