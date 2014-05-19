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

llvm::Type* Bool::GetLLVMType(Codegen::Generator& g) {
    return llvm::IntegerType::getInt8Ty(g.module->getContext());
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
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                    return bb.CreateOr(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(OrAssignOperator.get());
        case Lexer::TokenType::AndAssign:
            AndAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                    return bb.CreateAnd(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(AndAssignOperator.get());
        case Lexer::TokenType::XorAssign:
            XorAssignOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                    return bb.CreateXor(lhs, rhs);
                });
            }, { analyzer.GetLvalueType(this), this });
            return analyzer.GetOverloadSet(XorAssignOperator.get());
        }
    }
    switch(name) {
    case Lexer::TokenType::LT:
        LTOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                return bb.CreateICmpSLT(lhs, rhs);
            });
        }, { this, this });
        return analyzer.GetOverloadSet(LTOperator.get());
    case Lexer::TokenType::EqCmp:
        EQOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) -> std::unique_ptr<Expression> {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
                return bb.CreateICmpEQ(lhs, rhs);
            });
        }, { this, this });
        return analyzer.GetOverloadSet(EQOperator.get());
    case Lexer::TokenType::Negate:
        NegOperator = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c)->std::unique_ptr<Expression> {
            return CreatePrimUnOp(std::move(args[0]), c.from->analyzer.GetBooleanType(), [](llvm::Value* v, Codegen::Generator& g, llvm::IRBuilder<>& bb) {
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