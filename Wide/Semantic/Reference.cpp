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
clang::QualType Reference::GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) {
    return tu.GetASTContext().getLValueReferenceType(Pointee->GetClangType(tu, a));
}

Codegen::Expression* Reference::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return a.gen->CreateStore(mem, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}

std::size_t Reference::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerSize();
}
std::size_t Reference::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->GetDataLayout()).getPointerABIAlignment();
}

// Perform collapse
ConcreteExpression Reference::BuildRvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return ConcreteExpression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}
ConcreteExpression Reference::BuildLvalueConstruction(std::vector<ConcreteExpression> args, Analyzer& a) {
    if (args.size() == 0)
        throw std::runtime_error("Cannot default-construct a reference type.");
    if (args.size() == 1 && args[0].t->IsReference(Pointee))
        return ConcreteExpression(this, args[0].Expr);
    throw std::runtime_error("Attempt to construct a reference from something it could not be.");
}