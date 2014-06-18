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
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                    return con->CreateOr(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(OrAssignOperator.get());
        case Lexer::TokenType::AndAssign:
            AndAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                    return con->CreateAnd(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(AndAssignOperator.get());
        case Lexer::TokenType::XorAssign:
            XorAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                    return con->CreateXor(lhs, rhs);
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
                std::vector<Expression*> rhs_destructors;
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto val = con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con));
                    auto cur_block = con->GetInsertBlock();
                    auto false_br = llvm::BasicBlock::Create(con, "false", con->GetInsertBlock()->getParent());
                    auto true_br = llvm::BasicBlock::Create(con, "true", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(val, true_br, false_br);
                    con->SetInsertPoint(false_br);
                    CodegenContext rhscon(con);
                    auto false_val = con->CreateTrunc(rhs->GetValue(rhscon), llvm::Type::getInt1Ty(con));
                    if (rhscon.Destructors.size() != con.Destructors.size()) {
                        rhs_destructors = con.GetAddedDestructors(rhscon);
                        // If we might need something destroyed, then we need to be destroyed.
                        con.Destructors.push_back(this);
                    }
                    con->CreateBr(true_br);
                    false_br = con->GetInsertBlock();
                    con->SetInsertPoint(true_br);
                    auto phi = con->CreatePHI(llvm::Type::getInt1Ty(con), 2);
                    phi->addIncoming(val, cur_block);
                    phi->addIncoming(false_val, false_br);
                    return con->CreateZExt(phi, llvm::Type::getInt8Ty(con));
                }
                Type* GetType() override final {
                    return lhs->GetType(); // Both args are bool and we produce bool
                }
                void DestroyExpressionLocals(CodegenContext& con) override final {
                    assert(!rhs_destructors.empty());
                    auto false_bb = llvm::BasicBlock::Create(con, "false_destroy", con->GetInsertBlock()->getParent());
                    auto cont_bb = llvm::BasicBlock::Create(con, "cont_destroy", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con)), cont_bb, false_bb);
                    con->SetInsertPoint(false_bb);
                    for (auto rit = rhs_destructors.rbegin(); rit != rhs_destructors.rend(); ++rit) {
                        (*rit)->DestroyLocals(con);
                    }
                    con->CreateBr(cont_bb);
                    con->SetInsertPoint(cont_bb);
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
                std::vector<Expression*> rhs_destructors;
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto val = con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con));
                    auto cur_block = con->GetInsertBlock();
                    auto false_br = llvm::BasicBlock::Create(con, "false", con->GetInsertBlock()->getParent());
                    auto true_br = llvm::BasicBlock::Create(con, "true", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(val, false_br, true_br);
                    con->SetInsertPoint(false_br);
                    CodegenContext rhscon(con);
                    auto false_val = con->CreateTrunc(rhs->GetValue(rhscon), llvm::Type::getInt1Ty(con));
                    if (rhscon.Destructors.size() != con.Destructors.size()) {
                        rhs_destructors = con.GetAddedDestructors(rhscon);
                        // If we might need something destroyed, then we need to be destroyed.
                        con.Destructors.push_back(this);
                    }
                    con->CreateBr(true_br);
                    false_br = con->GetInsertBlock();
                    con->SetInsertPoint(true_br);
                    auto phi = con->CreatePHI(llvm::Type::getInt1Ty(con), 2);
                    phi->addIncoming(val, cur_block);
                    phi->addIncoming(false_val, false_br);
                    return con->CreateZExt(phi, llvm::Type::getInt8Ty(con));
                }
                Type* GetType() override final {
                    return lhs->GetType(); // Both args are bool and we produce bool
                }
                void DestroyExpressionLocals(CodegenContext& con) override final {
                    assert(!rhs_destructors.empty());
                    auto cont_bb = llvm::BasicBlock::Create(con, "cont_destroy", con->GetInsertBlock()->getParent());
                    auto true_bb = llvm::BasicBlock::Create(con, "true_destroy", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con)), true_bb, cont_bb);
                    con->SetInsertPoint(true_bb);
                    for (auto rit = rhs_destructors.rbegin(); rit != rhs_destructors.rend(); ++rit) {
                        (*rit)->DestroyLocals(con);
                    }
                    con->CreateBr(cont_bb);
                    con->SetInsertPoint(cont_bb);
                }
            };
            return Wide::Memory::MakeUnique<ShortCircuitAnd>(std::move(args[0]), std::move(args[1]));
        }, { this, this });
        return analyzer.GetOverloadSet(AndOperator.get());
    case Lexer::TokenType::LT:
        LTOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateZExt(con->CreateICmpSLT(lhs, rhs), llvm::Type::getInt8Ty(con));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(LTOperator.get());
    case Lexer::TokenType::EqCmp:
        EQOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateZExt(con->CreateICmpEQ(lhs, rhs), llvm::Type::getInt8Ty(con));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(EQOperator.get());
    case Lexer::TokenType::Negate:
        NegOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c)->std::unique_ptr<Expression> {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* v, CodegenContext& con) {
                return con->CreateNot(v);
            });
        }, { this });
        return analyzer.GetOverloadSet(NegOperator.get());
    }
    return PrimitiveType::CreateOperatorOverloadSet(t, name, access);
}

std::string Bool::explain() {
    return "bool";
}