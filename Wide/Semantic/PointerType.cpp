#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

clang::QualType PointerType::GetClangType(ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getPointerType(pointee->GetClangType(tu, a));
}

std::function<llvm::Type*(llvm::Module*)> PointerType::GetLLVMType(Analyzer& a) {
    return [=, &a](llvm::Module* mod) {
        auto ty = pointee->GetLLVMType(a)(mod);
        if (ty->isVoidTy())
            ty = llvm::IntegerType::getInt8Ty(mod->getContext());
        return llvm::PointerType::get(ty, 0);
    };
}
std::size_t PointerType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t PointerType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

PointerType::PointerType(Type* point) {
    pointee = point;
}

ConcreteExpression PointerType::BuildDereference(ConcreteExpression val, Context c) {
    return ConcreteExpression(c->GetLvalueType(pointee), val.BuildValue(c).Expr);
}

OverloadSet* PointerType::CreateConstructorOverloadSet(Analyzer& a) {
    auto usual = PrimitiveType::CreateConstructorOverloadSet(a);
    //return usual;
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(a.GetNullType());
    auto null = make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateNull(GetLLVMType(*c))));
    }, types, a);
    return a.GetOverloadSet(usual, a.GetOverloadSet(null)); 
}

Codegen::Expression* PointerType::BuildBooleanConversion(ConcreteExpression obj, Context c) {
    return c->gen->CreateNegateExpression(c->gen->CreateIsNullExpression(obj.BuildValue(c).Expr));
}
