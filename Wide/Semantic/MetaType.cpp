#include <Wide/Semantic/MetaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::size_t MetaType::size(Analyzer& a) { return a.gen->GetInt8AllocSize(); }
std::size_t MetaType::alignment(Analyzer& a) { return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8); }

std::function<llvm::Type*(llvm::Module*)> MetaType::GetLLVMType(Analyzer& a) {
    std::stringstream typenam;
    typenam << this;
    auto nam = typenam.str();
    return [=](llvm::Module* mod) -> llvm::Type* {
        if (mod->getTypeByName(nam))
            return mod->getTypeByName(nam);
        return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
    };
}

Codegen::Expression* MetaType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() > 1)
        throw std::runtime_error("Attempt to construct a type object with too many arguments.");
    if (args.size() == 1 && args[0].t->Decay() != this)
        throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
    return args.size() == 0 ? mem : a.gen->CreateChainExpression(args[0].Expr, mem);
}

ConcreteExpression MetaType::BuildValueConstruction(std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    if (args.size() > 1)
        throw std::runtime_error("Attempt to construct a type object with too many arguments.");
    if (args.size() == 1 && args[0].t->Decay() != this)
        throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
    return ConcreteExpression(this, args.size() == 0 ? (Codegen::Expression*)a.gen->CreateNull(GetLLVMType(a)) : a.gen->CreateChainExpression(args[0].Expr, a.gen->CreateNull(GetLLVMType(a))));
}