#include <Codegen/Statement.h>
#include <Codegen/Expression.h>

using namespace Wide;
using namespace Codegen;

#pragma warning(push, 0)

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#pragma warning(pop)

llvm::Value* ProcessBool(llvm::Value* val, llvm::IRBuilder<>& bb) {
    if (val->getType() == llvm::IntegerType::getInt1Ty(bb.getContext()))
        return val;
    if (val->getType() == llvm::IntegerType::getInt8Ty(bb.getContext()))
        return bb.CreateTrunc(val, llvm::IntegerType::getInt1Ty(bb.getContext()));
    throw std::runtime_error("Attempted to code generate a boolean expression that was neither i1 nor i8.");
}

ReturnStatement::ReturnStatement(Expression* e) 
    : val(e) {}

ReturnStatement::ReturnStatement() 
    : val(nullptr) {}

void ReturnStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    if (!val)
        bb.CreateRetVoid();
    else {
        auto llvmval = val->GetValue(bb, g);
        if (llvmval->getType() != llvm::Type::getVoidTy(bb.getContext()))
            bb.CreateRet(val->GetValue(bb, g));
        else
            bb.CreateRetVoid();
    }
}

Codegen::Expression* ReturnStatement::GetReturnExpression() {
    return val;
}

void IfStatement::Build(llvm::IRBuilder<>& bb, Generator& g) {
    auto true_bb = llvm::BasicBlock::Create(bb.getContext(), "true_bb", bb.GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    auto else_bb = false_br ? llvm::BasicBlock::Create(bb.getContext(), "false_bb", bb.GetInsertBlock()->getParent()) : continue_bb;

    bb.CreateCondBr(ProcessBool(condition->GetValue(bb, g), bb), true_bb, else_bb);
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
    auto check_bb = llvm::BasicBlock::Create(bb.getContext(), "check_bb", bb.GetInsertBlock()->getParent());
    auto loop_bb = llvm::BasicBlock::Create(bb.getContext(), "loop_bb", bb.GetInsertBlock()->getParent());
    auto continue_bb = llvm::BasicBlock::Create(bb.getContext(), "continue_bb", bb.GetInsertBlock()->getParent());
    bb.CreateBr(check_bb);
    bb.SetInsertPoint(check_bb);
    bb.CreateCondBr(ProcessBool(cond->GetValue(bb, g), bb), loop_bb, continue_bb);
    bb.SetInsertPoint(loop_bb);
    body->Build(bb, g);
    if (!bb.GetInsertBlock()->back().isTerminator())
        bb.CreateBr(check_bb);
    bb.SetInsertPoint(continue_bb);
}