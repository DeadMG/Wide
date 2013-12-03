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

Codegen::Expression* Reference::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return c->gen->CreateStore(mem, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}

// Perform collapse
ConcreteExpression Reference::BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return ConcreteExpression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}
ConcreteExpression Reference::BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return ConcreteExpression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}
bool RvalueType::IsA(Type* other) {
    if (other == this)
        return true;
    if (IsMovable())
        return Decay()->IsA(other);
    return false;
}
bool LvalueType::IsA(Type* other) {
    if (other == this)
        return true;
    if (IsCopyable())
        return Decay()->IsA(other);
    return false;
}