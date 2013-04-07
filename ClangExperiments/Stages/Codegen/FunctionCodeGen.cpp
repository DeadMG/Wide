// Silly name because MSVC can't cope with same-name CPP files even when in a different folder.

#include "Function.h"
#include "Statement.h"

using namespace Wide;
using namespace Codegen;

#pragma warning(push, 0)

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/Verifier.h>

#pragma warning(pop)

void Function::EmitCode(llvm::Module* mod, llvm::LLVMContext& con) {
    llvm::Function* f = nullptr;
    if (f = mod->getFunction(name)) {
        if (f->getType() == Type(mod))
            f = mod->getFunction(name);
        else
            throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
    } else {
        f = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(Type(mod))->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, name, mod);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(con, "entry", f);
    llvm::IRBuilder<> irbuilder(bb);
    for(auto&& x : statements)
        x->Build(irbuilder);

    llvm::verifyFunction(*f);
}

Function::Function(std::function<llvm::Type*(llvm::Module*)> ret, std::string name)
    : Type(ret)
    , name(std::move(name))
{}