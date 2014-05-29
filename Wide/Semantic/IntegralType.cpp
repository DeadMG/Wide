#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

IntegralType::IntegralType(unsigned bit, bool sign, Analyzer& a)
: bits(bit), is_signed(sign), PrimitiveType(a) {
    assert(bits == 8 || bits == 16 || bits == 32 || bits == 64);
}
Wide::Util::optional<clang::QualType> IntegralType::GetClangType(ClangTU& TU) {
    switch(bits) {
    case 8:
        if (is_signed)
            return TU.GetASTContext().CharTy;
        else
            return TU.GetASTContext().UnsignedCharTy;
    case 16:
        if (is_signed)
            return TU.GetASTContext().ShortTy;
        else
            return TU.GetASTContext().UnsignedShortTy;
    case 32:
        if (is_signed)
            return TU.GetASTContext().IntTy;
        else
            return TU.GetASTContext().UnsignedIntTy;
    case 64:
        if (is_signed)
            return TU.GetASTContext().LongLongTy;
        else
            return TU.GetASTContext().UnsignedLongLongTy;
    }
    assert(false && "Integral types only go up to 64bit.");
}
llvm::Type* IntegralType::GetLLVMType(llvm::Module* module) {
    return llvm::IntegerType::get(module->getContext(), bits);
}

OverloadSet* IntegralType::CreateConstructorOverloadSet(Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(Lexer::Access::Public);
    struct integral_constructor : public OverloadResolvable, Callable {
        integral_constructor(IntegralType* self)
            : integral(self) {}

        IntegralType* integral;
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            if (types[0] != a.GetLvalueType(integral)) return Util::none;
            auto intty = dynamic_cast<IntegralType*>(types[1]->Decay());
            if (!intty) return Util::none;
            if (intty->bits > integral->bits && intty->is_signed != integral->is_signed) return Util::none;
            return types;
        }
        Callable* GetCallableForResolution(std::vector<Type*> types, Analyzer& a) override final { return this; }
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            args[1] = BuildValue(std::move(args[1]));
            if (args[1]->GetType() == integral)
                return Wide::Memory::MakeUnique<ImplicitStoreExpr>(std::move(args[0]), std::move(args[1]));
            auto inttype = dynamic_cast<IntegralType*>(args[1]->GetType());
            if (integral->bits < inttype->bits)
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateTrunc(rhs, integral->GetLLVMType(module));
                });
            //    return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateTruncate(args[1].Expr, integral->GetLLVMType(*c))));
            if (integral->is_signed && inttype->is_signed)
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateSExt(rhs, integral->GetLLVMType(module));
                });
            if (!integral->is_signed && !inttype->is_signed)
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return bb.CreateZExt(rhs, integral->GetLLVMType(module));
                });
            if (integral->bits == inttype->bits)
                return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), [this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                    return rhs;
                }); 
            assert(false && "Integer constructor called with conditions that OR should have prevented.");
            return nullptr;
        }
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            return args;
        }
    };
    if (!ConvertingConstructor) ConvertingConstructor = Wide::Memory::MakeUnique<integral_constructor>(this);
    return analyzer.GetOverloadSet(ConvertingConstructor.get());
}

std::size_t IntegralType::size() {
    return (bits / 8);
}

std::size_t IntegralType::alignment() {
    return analyzer.GetDataLayout().getABIIntegerTypeAlignment(bits);
}

OverloadSet* IntegralType::CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access) {
    if (access != Lexer::Access::Public) return CreateADLOverloadSet(name, lhs, rhs, Lexer::Access::Public);
    auto CreateAssOp = [this](std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>& bb, llvm::IRBuilder<>&)> func) {
        return MakeResolvable([this, func](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimAssOp(std::move(args[0]), std::move(args[1]), func);
        }, { analyzer.GetLvalueType(this), this });
    };
    auto CreateOp = [this](std::function<llvm::Value*(llvm::Value*, llvm::Value*, llvm::Module*, llvm::IRBuilder<>& bb, llvm::IRBuilder<>&)> func) {
        return MakeResolvable([this, func](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), func);
        }, { this, this });
    };
    switch (name) {
    case Lexer::TokenType::RightShiftAssign:
        RightShiftAssign = CreateAssOp([this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            if (is_signed)
                return bb.CreateAShr(lhs, rhs);
            return bb.CreateLShr(lhs, rhs);
        });
        return analyzer.GetOverloadSet(RightShiftAssign.get());
    case Lexer::TokenType::LeftShiftAssign:
        LeftShiftAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateShl(lhs, rhs);
        });
        return analyzer.GetOverloadSet(LeftShiftAssign.get());
    case Lexer::TokenType::MulAssign:
        MulAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateMul(lhs, rhs);
        });
        return analyzer.GetOverloadSet(MulAssign.get());
    case Lexer::TokenType::PlusAssign:
        PlusAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateAdd(lhs, rhs);
        });
        return analyzer.GetOverloadSet(PlusAssign.get());
    case Lexer::TokenType::OrAssign:
        OrAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateOr(lhs, rhs);
        });
        return analyzer.GetOverloadSet(OrAssign.get());
    case Lexer::TokenType::AndAssign:
        AndAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateAnd(lhs, rhs);
        });
        return analyzer.GetOverloadSet(AndAssign.get());
    case Lexer::TokenType::XorAssign:
        XorAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateXor(lhs, rhs);
        });
        return analyzer.GetOverloadSet(XorAssign.get());
    case Lexer::TokenType::MinusAssign:
        MinusAssign = CreateAssOp([](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            return bb.CreateSub(lhs, rhs);
        });
        return analyzer.GetOverloadSet(MinusAssign.get());
    case Lexer::TokenType::ModAssign:
        ModAssign = CreateAssOp([this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            if (is_signed)
                return bb.CreateSRem(lhs, rhs);
            return bb.CreateURem(lhs, rhs);
        });
        return analyzer.GetOverloadSet(ModAssign.get());
    case Lexer::TokenType::DivAssign:
        DivAssign = CreateAssOp([this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
            if (is_signed)
                return bb.CreateSDiv(lhs, rhs);
            return bb.CreateUDiv(lhs, rhs);
        });
        return analyzer.GetOverloadSet(DivAssign.get());
    case Lexer::TokenType::LT:
        LT = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [this](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                llvm::Value* result = is_signed ? bb.CreateICmpSLT(lhs, rhs) : bb.CreateICmpULT(lhs, rhs);
                return bb.CreateZExt(result, llvm::Type::getInt8Ty(module->getContext()));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(LT.get());
    case Lexer::TokenType::EqCmp:
        EQ = MakeResolvable([](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimOp(std::move(args[0]), std::move(args[1]), c.from->analyzer.GetBooleanType(), [](llvm::Value* lhs, llvm::Value* rhs, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                return bb.CreateZExt(bb.CreateICmpEQ(lhs, rhs), llvm::Type::getInt8Ty(module->getContext()));
            });
        }, { this, this });
        return analyzer.GetOverloadSet(EQ.get());
    case Lexer::TokenType::Increment:
        Increment = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(std::move(args[0]), analyzer.GetLvalueType(this), [](llvm::Value* val, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                bb.CreateStore(bb.CreateAdd(bb.CreateLoad(val), llvm::ConstantInt::get(val->getType(), llvm::APInt(64, 1, false))), val);
                return val;
            });
        }, { analyzer.GetLvalueType(this) });
        return analyzer.GetOverloadSet(Increment.get());
    }
    return PrimitiveType::CreateADLOverloadSet(name, lhs, rhs, access);
}
bool IntegralType::IsA(Type* self, Type* other, Lexer::Access access) {
    // If we already are, then don't bother.
    if (Type::IsA(self, other, access)) return true;

    // T to U conversion
    // Cannot be U&
    if (IsLvalueType(other)) return false;

    auto otherint = dynamic_cast<IntegralType*>(other->Decay());
    if (!otherint)
        return false;

    if (is_signed == otherint->is_signed && otherint->bits > bits)
        return true;
    if (!is_signed && otherint->is_signed && otherint->bits > bits)
        return true;
    return false;
}
OverloadSet* IntegralType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access) {
    if (access != Lexer::Access::Public)
        return AccessMember(self, what, Lexer::Access::Public);
    switch (what) {
    case Lexer::TokenType::Increment:
        Increment = MakeResolvable([this](std::vector<std::unique_ptr<Expression>> args, Context c) {
            return CreatePrimUnOp(std::move(args[0]), analyzer.GetLvalueType(this), [this](llvm::Value* self, llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) {
                bb.CreateStore(bb.CreateAdd(bb.CreateLoad(self), llvm::ConstantInt::get(GetLLVMType(module), 1, is_signed)), self);
                return self;
            });
        }, { analyzer.GetLvalueType(this) });
        return analyzer.GetOverloadSet(Increment.get());
    }
    return PrimitiveType::CreateOperatorOverloadSet(self, what, access);
}
std::string IntegralType::explain() {
    auto name = "int" + std::to_string(bits);
    return !is_signed ? "u" + name : name;
}