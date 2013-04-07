#include "Statement.h"
#include "Expression.h"

using namespace Wide;
using namespace Codegen;

#pragma warning(push, 0)

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

ReturnStatement::ReturnStatement(Expression* e) 
    : val(e) {}

ReturnStatement::ReturnStatement() 
    : val(nullptr) {}

void ReturnStatement::Build(llvm::IRBuilder<>& bb) {
    if (!val)
        bb.CreateRetVoid();
    else {
        auto llvmval = val->GetValue(bb);
        if (llvmval->getType() != llvm::Type::getVoidTy(bb.getContext()))
            bb.CreateRet(val->GetValue(bb));
        else
            bb.CreateRetVoid();
    }
}

Codegen::Expression* ReturnStatement::GetReturnExpression() {
    return val;
}

void IfStatement::Build(llvm::IRBuilder<>& bb) {
    auto true_bb = llvm::BasicBlock::Create(bb.getContext(), "true_bb", bb.GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    auto else_bb = false_br ? llvm::BasicBlock::Create(bb.getContext(), "false_bb", bb.GetInsertBlock()->getParent()) : continue_bb;

    bb.CreateCondBr(condition->GetValue(bb), true_bb, else_bb);
    bb.SetInsertPoint(true_bb);
    // May have caused it's own branches to other basic blocks.
    true_br->Build(bb);
    bb.CreateBr(continue_bb);
    
    if (false_br) {
        bb.SetInsertPoint(else_bb);
        false_br->Build(bb);
        bb.CreateBr(continue_bb);
    }

    bb.SetInsertPoint(continue_bb);
}

void WhileStatement::Build(llvm::IRBuilder<>& bb) {
    auto check_bb = llvm::BasicBlock::Create(bb.getContext(), "check_bb", bb.GetInsertBlock()->getParent());
    auto loop_bb = llvm::BasicBlock::Create(bb.getContext(), "loop_bb", bb.GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    bb.CreateBr(check_bb);
    bb.SetInsertPoint(check_bb);
    bb.CreateCondBr(cond->GetValue(bb), loop_bb, continue_bb);
    bb.SetInsertPoint(loop_bb);
    body->Build(bb);
    bb.CreateBr(check_bb);
    bb.SetInsertPoint(continue_bb);
}