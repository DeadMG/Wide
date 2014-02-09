#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Reference.h>

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::function<llvm::Type*(llvm::Module*)> Reference::GetLLVMType(Analyzer& a) {
    auto f = Decay()->GetLLVMType(a);
    return [=](llvm::Module* m) {
        return f(m)->getPointerTo();
    };
}
clang::QualType LvalueType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getLValueReferenceType(Decay()->GetClangType(tu, a));
}
clang::QualType RvalueType::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getRValueReferenceType(Decay()->GetClangType(tu, a));
}

std::size_t Reference::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t Reference::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

bool RvalueType::IsA(Type* other, Analyzer& a) {
    if (other == this)
        return true;
    if (Decay()->IsA(other, a))
        return Decay()->IsMoveConstructible(a);
    return false;
}
bool LvalueType::IsA(Type* other, Analyzer& a) {
    if (other == this)
        return true;
    if (other == a.GetRvalueType(Decay()))
        return false;
    if (Decay()->IsA(other, a))
        return Decay()->IsCopyConstructible(a);
    return false;
}
OverloadSet* RvalueType::CreateConstructorOverloadSet(Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this)); 
    types.push_back(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
    return a.GetOverloadSet(set);
}
OverloadSet* LvalueType::CreateConstructorOverloadSet(Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(this);
    std::unordered_set<OverloadResolvable*> set;
    set.insert(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
    return a.GetOverloadSet(set);
}
ConcreteExpression RvalueType::BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (args.size() == 1) {
        if (args[0].t == Decay()) {
            auto mem = c->gen->CreateVariable(Decay()->GetLLVMType(*c), Decay()->alignment(*c));
            auto store = c->gen->CreateStore(mem, args[0].Expr);
            return ConcreteExpression(this, c->gen->CreateChainExpression(store, mem));
        }
    }
    return Type::BuildValueConstruction(args, c);
}