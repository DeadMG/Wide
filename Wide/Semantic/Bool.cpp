#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Lexer/Token.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

llvm::Type* Bool::GetLLVMType(llvm::Module* module) {
    return llvm::IntegerType::getInt8Ty(module->getContext());
}

Wide::Util::optional<clang::QualType> Bool::GetClangType(ClangTU& where) {
    return where.GetASTContext().BoolTy;
}

std::size_t Bool::size() {
    return 1;
}

std::size_t Bool::alignment() {
    return analyzer.GetDataLayout().getABIIntegerTypeAlignment(8);
}

std::unique_ptr<Expression> Bool::BuildBooleanConversion(std::unique_ptr<Expression> arg, Context c) {
    return BuildValue(std::move(arg));
}

OverloadSet* Bool::CreateOperatorOverloadSet(Type* t, Lexer::TokenType name, Lexer::Access access) {
    if (access != Lexer::Access::Public)
        return AccessMember(t, name, Lexer::Access::Public);

    if (t == analyzer.GetLvalueType(this)) {
        switch (name) {
        case Lexer::TokenType::OrAssign:
            OrAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateOr(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(OrAssignOperator.get());
        case Lexer::TokenType::AndAssign:
            AndAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateAnd(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(AndAssignOperator.get());
        case Lexer::TokenType::XorAssign:
            XorAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateXor(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(XorAssignOperator.get());
        }
    }
    switch (name) {
    case Lexer::TokenType::Or:
        OrOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            struct ShortCircuitOr : Expression {
                ShortCircuitOr(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs)
                : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
                std::unique_ptr<Expression> lhs, rhs;
                llvm::Value* ComputeValue(llvm::Module* m, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    auto val = bb.CreateTrunc(lhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext()));
                    auto cur_block = bb.GetInsertBlock();
                    auto false_br = llvm::BasicBlock::Create(m->getContext(), "false", bb.GetInsertBlock()->getParent());
                    auto true_br = llvm::BasicBlock::Create(m->getContext(), "true", bb.GetInsertBlock()->getParent());
                    bb.CreateCondBr(val, true_br, false_br);
                    bb.SetInsertPoint(false_br);
                    auto false_val = bb.CreateTrunc(rhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext()));
                    bb.CreateBr(true_br);
                    bb.SetInsertPoint(true_br);
                    auto phi = bb.CreatePHI(llvm::Type::getInt1Ty(m->getContext()), 2);
                    phi->addIncoming(val, cur_block);
                    phi->addIncoming(false_val, false_br);
                    return bb.CreateZExt(phi, llvm::Type::getInt8Ty(m->getContext()));
                }
                Type* GetType() override final {
                    return lhs->GetType(); // Both args are bool and we produce bool
                }
                void DestroyExpressionLocals(llvm::Module* m, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    lhs->DestroyLocals(m, bb, allocas);
                    auto false_bb = llvm::BasicBlock::Create(m->getContext(), "false_destroy", bb.GetInsertBlock()->getParent());
                    auto cont_bb = llvm::BasicBlock::Create(m->getContext(), "cont_destroy", bb.GetInsertBlock()->getParent());
                    bb.CreateCondBr(bb.CreateTrunc(lhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext())), cont_bb, false_bb);
                    bb.SetInsertPoint(false_bb);
                    rhs->DestroyLocals(m, bb, allocas);
                    bb.CreateBr(cont_bb);
                    bb.SetInsertPoint(cont_bb);
                }
            };
            return Wide::Memory::MakeUnique<ShortCircuitOr>(std::move(args[0]), std::move(args[1]));
        }, { this, this });
        return analyzer.GetOverloadSet(OrOperator.get());
    case Lexer::TokenType::And:
        AndOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            struct ShortCircuitAnd : Expression {
                ShortCircuitAnd(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> rhs)
                : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
                std::unique_ptr<Expression> lhs, rhs;
                llvm::Value* ComputeValue(llvm::Module* m, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    auto val = bb.CreateTrunc(lhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext()));
                    auto cur_block = bb.GetInsertBlock();
                    auto true_br = llvm::BasicBlock::Create(m->getContext(), "true", bb.GetInsertBlock()->getParent());
                    auto false_br = llvm::BasicBlock::Create(m->getContext(), "false", bb.GetInsertBlock()->getParent());
                    bb.CreateCondBr(val, false_br, true_br);
                    bb.SetInsertPoint(false_br);
                    auto false_val = bb.CreateTrunc(rhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext()));
                    bb.CreateBr(true_br);
                    bb.SetInsertPoint(true_br);
                    auto phi = bb.CreatePHI(llvm::Type::getInt1Ty(m->getContext()), 2);
                    phi->addIncoming(val, cur_block);
                    phi->addIncoming(false_val, false_br);
                    return bb.CreateZExt(phi, llvm::Type::getInt8Ty(m->getContext()));
                }
                Type* GetType() override final {
                    return lhs->GetType(); // Both args are bool and we produce bool
                }
                void DestroyExpressionLocals(llvm::Module* m, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final {
                    lhs->DestroyLocals(m, bb, allocas);
                    auto cont_bb = llvm::BasicBlock::Create(m->getContext(), "cont_destroy", bb.GetInsertBlock()->getParent());
                    auto true_bb = llvm::BasicBlock::Create(m->getContext(), "true_destroy", bb.GetInsertBlock()->getParent());
                    bb.CreateCondBr(bb.CreateTrunc(lhs->GetValue(m, bb, allocas), llvm::Type::getInt1Ty(m->getContext())), true_bb, cont_bb);
                    bb.SetInsertPoint(true_bb);
                    rhs->DestroyLocals(m, bb, allocas);
                    bb.CreateBr(cont_bb);
                    bb.SetInsertPoint(cont_bb);
                }
            };
            return Wide::Memory::MakeUnique<ShortCircuitAnd>(std::move(args[0]), std::move(args[1]));
        }, { this, this });
        return analyzer.GetOverloadSet(AndOperator.get());
    case Lexer::TokenType::LT:
        LTOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                return bb.CreateZExt(bb.CreateICmpSLT(lhs, rhs), llvm::Type::getInt8Ty(module->getContext()));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(LTOperator.get());
    case Lexer::TokenType::EqCmp:
        EQOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                return bb.CreateZExt(bb.CreateICmpEQ(lhs, rhs), llvm::Type::getInt8Ty(module->getContext()));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(EQOperator.get());
    case Lexer::TokenType::Negate:
        NegOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c)->std::unique_ptr<Expression> {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* v, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                return bb.CreateNot(v);
            });
        }, { this });
        return analyzer.GetOverloadSet(NegOperator.get());
    }
    return PrimitiveType::CreateOperatorOverloadSet(t, name, access);
}

std::string Bool::explain() {
    return "bool";
}