#include "Expression.h"
#include "Generator.h"
#include "Function.h"
#include <set>
#include <array>

#pragma warning(push, 0)

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

#ifdef _DEBUG
#ifdef UNICODE
#undef UNICODE
#endif
#ifdef _UNICODE
#undef _UNICODE
#endif
#include <Windows.h>
#endif

using namespace Wide;
using namespace Codegen;

llvm::Value* Expression::GetValue(llvm::IRBuilder<>& bb, Generator& g){
    if (!val) 
        val = ComputeValue(bb, g);
    return val;
}
void Expression::Build(llvm::IRBuilder<>& bb, Generator& g) {
    GetValue(bb, g);
}

/*namespace Wide {
    namespace Codegen {
        std::string debug(ChainExpression* e, std::string indent = "") {
           std::string out = indent + "ChainExpression {\n";
           out += "s = " + debug(e->s, indent + "    ");
           out += "next = " + debug(e->next, indent + "    ");
           out += indent + "}\n";
           return out;
        }
        std::string debug(ChainStatement*, std::string indent = "");
        std::string debug(FieldExpression*, std::string indent = "");
        std::string debug(FunctionCall*, std::string indent = "");
        std::string debug(FunctionValue*, std::string indent = "");
        std::string debug(IfStatement*, std::string indent = "");
        std::string debug(Int8Expression*, std::string indent = "");
        std::string debug(IntegralLeftShiftExpression*, std::string indent = "");
        std::string debug(IntegralLessThan*, std::string indent = "");
        std::string debug(IntegralRightShiftExpression*, std::string indent = "");
        std::string debug(LoadExpression*, std::string indent = "");
        std::string debug(NamedGlobalVariable*, std::string indent = "");
        std::string debug(ParamExpression*, std::string indent = "");
        std::string debug(ReturnStatement*, std::string indent = "");
        std::string debug(StoreExpression*, std::string indent = "");
        std::string debug(StringExpression*, std::string indent = "");
        std::string debug(TruncateExpression*, std::string indent = "");
        std::string debug(Variable*, std::string indent = "");
        std::string debug(WhileStatement*, std::string indent = "");
        std::string debug(ZExt*, std::string indent = "");
    }
}
void Codegen::debug(Statement* s) {
    if (auto x = dynamic_cast<ChainExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<ChainStatement*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<FieldExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<FunctionCall*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<FunctionValue*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<IfStatement*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<Int8Expression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<IntegralLeftShiftExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<IntegralLessThan*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<IntegralRightShiftExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<LoadExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<NamedGlobalVariable*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<ParamExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<ReturnStatement*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<StoreExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<StringExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<TruncateExpression*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<Variable*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<WhileStatement*>(s)) OutputDebugString(debug(x).c_str());
    if (auto x = dynamic_cast<ZExt*>(s)) OutputDebugString(debug(x).c_str());
    __debugbreak();
}*/

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
    auto val = obj->GetValue(builder, g);
    if (val->getType()->isPointerTy())
        return builder.CreateStructGEP(obj->GetValue(builder, g), fieldnum);
    else {
        std::vector<uint32_t> args;
        args.push_back(fieldnum);
        return builder.CreateExtractValue(val, args);
    }
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

llvm::Value* IntegralLessThan::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    // Semantic will expect an i8 here, not an i1.
    return builder.CreateZExt(builder.CreateICmpSLT(lhs->GetValue(builder, g), rhs->GetValue(builder, g)), llvm::IntegerType::getInt8Ty(builder.getContext()));
}

llvm::Value* ZExt::ComputeValue(llvm::IRBuilder<>& builder, Generator& g) {
    return builder.CreateZExt(from->GetValue(builder, g), to(builder.GetInsertBlock()->getParent()->getParent()));
}