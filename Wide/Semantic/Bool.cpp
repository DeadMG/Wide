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

OverloadSet* Bool::CreateOperatorOverloadSet(Lexer::TokenType name, Parse::Access access) {
    if (access != Parse::Access::Public)
        return AccessMember(name, Parse::Access::Public);

    if (name == &Lexer::TokenTypes::QuestionMark) {
        BooleanConversion = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) {
            return args[0];
        }, { this });
        return analyzer.GetOverloadSet(BooleanConversion.get());
    }
    if (name == &Lexer::TokenTypes::OrAssign) {
        OrAssignOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateOr(lhs, rhs);
            });
        }, { analyzer.GetLvalueType(this), this });
        return analyzer.GetOverloadSet(OrAssignOperator.get());
    } else if (name == &Lexer::TokenTypes::AndAssign) {
        AndAssignOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateAnd(lhs, rhs);
            });
        }, { analyzer.GetLvalueType(this), this });
        return analyzer.GetOverloadSet(AndAssignOperator.get());
    } else if (name == &Lexer::TokenTypes::XorAssign) {
        XorAssignOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateXor(lhs, rhs);
            });
        }, { analyzer.GetLvalueType(this), this });
        return analyzer.GetOverloadSet(XorAssignOperator.get());
    } else if (name == &Lexer::TokenTypes::Or) {
        OrOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            struct ShortCircuitOr : Expression {
                ShortCircuitOr(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs)
                : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
                std::shared_ptr<Expression> lhs, rhs;
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto val = con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con));
                    auto cur_block = con->GetInsertBlock();
                    auto false_br = llvm::BasicBlock::Create(con, "false", con->GetInsertBlock()->getParent());
                    auto true_br = llvm::BasicBlock::Create(con, "true", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(val, true_br, false_br);
                    con->SetInsertPoint(false_br);
                    CodegenContext rhscon(con);
                    auto false_val = con->CreateTrunc(rhs->GetValue(rhscon), llvm::Type::getInt1Ty(con));
                    auto rhs_destructors = con.GetAddedDestructors(rhscon);
                    if (!rhs_destructors.empty()) {
                        con.AddDestructor([=](CodegenContext& con) {
                            auto false_bb = llvm::BasicBlock::Create(con, "false_destroy", con->GetInsertBlock()->getParent());
                            auto cont_bb = llvm::BasicBlock::Create(con, "cont_destroy", con->GetInsertBlock()->getParent());
                            con->CreateCondBr(con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con)), cont_bb, false_bb);
                            con->SetInsertPoint(false_bb);
                            for (auto rit = rhs_destructors.rbegin(); rit != rhs_destructors.rend(); ++rit) {
                                rit->first(con);
                            }
                            con->CreateBr(cont_bb);
                            con->SetInsertPoint(cont_bb);
                        });
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
            };
            return Wide::Memory::MakeUnique<ShortCircuitOr>(std::move(args[0]), std::move(args[1]));
        }, { this, this });
        return analyzer.GetOverloadSet(OrOperator.get());
    } else if (name == &Lexer::TokenTypes::And) {
        AndOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            struct ShortCircuitAnd : Expression {
                ShortCircuitAnd(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs)
                : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
                std::shared_ptr<Expression> lhs, rhs;
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto val = con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con));
                    auto cur_block = con->GetInsertBlock();
                    auto false_br = llvm::BasicBlock::Create(con, "true", con->GetInsertBlock()->getParent());
                    auto true_br = llvm::BasicBlock::Create(con, "false", con->GetInsertBlock()->getParent());
                    con->CreateCondBr(val, false_br, true_br);
                    con->SetInsertPoint(false_br);
                    CodegenContext rhscon(con);
                    auto false_val = con->CreateTrunc(rhs->GetValue(rhscon), llvm::Type::getInt1Ty(con));
                    auto rhs_destructors = con.GetAddedDestructors(rhscon);
                    if (!rhs_destructors.empty()) {
                        con.AddDestructor([=](CodegenContext& con) {
                            auto cont_bb = llvm::BasicBlock::Create(con, "cont_destroy", con->GetInsertBlock()->getParent());
                            auto true_bb = llvm::BasicBlock::Create(con, "true_destroy", con->GetInsertBlock()->getParent());
                            con->CreateCondBr(con->CreateTrunc(lhs->GetValue(con), llvm::Type::getInt1Ty(con)), true_bb, cont_bb);
                            con->SetInsertPoint(true_bb);
                            for (auto rit = rhs_destructors.rbegin(); rit != rhs_destructors.rend(); ++rit) {
                                rit->first(con);
                            }
                            con->CreateBr(cont_bb);
                            con->SetInsertPoint(cont_bb);
                        });
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
            };
            return Wide::Memory::MakeUnique<ShortCircuitAnd>(std::move(args[0]), std::move(args[1]));
        }, { this, this });
        return analyzer.GetOverloadSet(AndOperator.get());
    } else if (name == &Lexer::TokenTypes::LT) {
        LTOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateZExt(con->CreateICmpSLT(lhs, rhs), llvm::Type::getInt8Ty(con));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(LTOperator.get());
    } else if (name == &Lexer::TokenTypes::EqCmp) {
        EQOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c) -> std::shared_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, CodegenContext& con) {
                return con->CreateZExt(con->CreateICmpEQ(lhs, rhs), llvm::Type::getInt8Ty(con));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(EQOperator.get());
    } else if (name == &Lexer::TokenTypes::Negate) {
        NegOperator = MakeResolvable([](std::vector<std::shared_ptr<Expression>> args, Context c)->std::shared_ptr<Expression> {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* v, CodegenContext& con) {
                return con->CreateNot(v);
            });
        }, { this });
        return analyzer.GetOverloadSet(NegOperator.get());
    }
    return PrimitiveType::CreateOperatorOverloadSet(name, access);
}

std::string Bool::explain() {
    return "bool";
}