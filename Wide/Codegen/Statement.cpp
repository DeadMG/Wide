#include <Wide/Codegen/Statement.h>
#include <Wide/Codegen/Expression.h>
#include <stdexcept>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace LLVMCodegen;

llvm::Value* ProcessBool(llvm::Value* val, llvm::IRBuilder<>& bb) {
    if (val->getType() == llvm::IntegerType::getInt1Ty(bb.getContext()))
        return val;
    if (val->getType() == llvm::IntegerType::getInt8Ty(bb.getContext()))
        return bb.CreateTrunc(val, llvm::IntegerType::getInt1Ty(bb.getContext()));
    throw std::runtime_error("Attempted to code generate a boolean expression that was neither i1 nor i8.");
}

ReturnStatement::ReturnStatement(std::function<LLVMCodegen::Expression*()> e)
    : val(e) {}

ReturnStatement::ReturnStatement() 
    : val(nullptr) {}

void ReturnStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    if (!val)
        bb.CreateRetVoid();
    else {
        auto llvmval = val()->GetValue(bb, g);
        if (llvmval->getType() != llvm::Type::getVoidTy(bb.getContext())) {
            if (llvmval->getType() == llvm::IntegerType::getInt1Ty(bb.GetInsertBlock()->getContext())) {
                llvmval = bb.CreateZExt(llvmval, llvm::IntegerType::getInt8Ty(bb.GetInsertBlock()->getContext()));
            }
            bb.CreateRet(llvmval);
        }
        else
            bb.CreateRetVoid();
    }
}

Codegen::Expression* ReturnStatement::GetReturnExpression() {
    auto p = dynamic_cast<Codegen::Expression*>(val());
    assert(p);
    return p;
}

void Deferred::Build(llvm::IRBuilder<>& b, Generator& g) {
    auto expr = func();
    auto realstatement = dynamic_cast<LLVMCodegen::Statement*>(expr);
    assert(realstatement);
    realstatement->Build(b, g);
}

void IfStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    auto true_bb = llvm::BasicBlock::Create(bb.getContext(), "true_bb", bb.GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    auto else_bb = false_br ? llvm::BasicBlock::Create(bb.getContext(), "false_bb", bb.GetInsertBlock()->getParent()) : continue_bb;

    bb.CreateCondBr(ProcessBool(condition()->GetValue(bb, g), bb), true_bb, else_bb);
    bb.SetInsertPoint(true_bb);
    // May have caused it's own branches to other basic blocks.
    true_br->Build(bb, g);
    if (!bb.GetInsertBlock()->back().isTerminator())
        bb.CreateBr(continue_bb);
    
    if (false_br) {
        bb.SetInsertPoint(else_bb);
        false_br->Build(bb, g);
        if (!bb.GetInsertBlock()->back().isTerminator())
            bb.CreateBr(continue_bb);
    }

    bb.SetInsertPoint(continue_bb);
}

void WhileStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    check_bb = llvm::BasicBlock::Create(bb.getContext(), "check_bb", bb.GetInsertBlock()->getParent());
    auto loop_bb = llvm::BasicBlock::Create(bb.getContext(), "loop_bb", bb.GetInsertBlock()->getParent());
    continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    bb.CreateBr(check_bb);
    bb.SetInsertPoint(check_bb);
    bb.CreateCondBr(ProcessBool(cond()->GetValue(bb, g), bb), loop_bb, continue_bb);
    bb.SetInsertPoint(loop_bb);
    body->Build(bb, g);
    if (!bb.GetInsertBlock()->back().isTerminator())
        bb.CreateBr(check_bb);
    bb.SetInsertPoint(continue_bb);
}

void ContinueStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    bb.CreateBr(cont->GetCheckBlock());
}

void BreakStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    bb.CreateBr(cont->GetContinueBlock());
}