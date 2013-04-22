// Silly name because MSVC can't cope with same-name CPP files even when in a different folder.

#include "Function.h"
#include "Statement.h"
#include "Generator.h"

using namespace Wide;
using namespace Codegen;

#pragma warning(push, 0)

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/Verifier.h>

#pragma warning(pop)

void Function::EmitCode(llvm::Module* mod, llvm::LLVMContext& con, Generator& g) {
    llvm::Function* f = nullptr;
    if (f = mod->getFunction(name)) {
        if (f->getType() == Type(mod))
            f = mod->getFunction(name);
        else {
			// If an i8/i1 mismatch, and we are a trampoline, just fix up the return statement and change the type. Else, fuck.			
			if (!tramp || !(f->getReturnType() == llvm::IntegerType::getInt1Ty(con) && llvm::dyn_cast<llvm::FunctionType>(Type(mod))->getReturnType() == llvm::IntegerType::getInt8Ty(con)))
				throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
			// Expect one return statement in a trampoline. Might modify if cause to later.
			assert(statements.size() == 1 && "A trampoline must have only one return statement so that the codegen can fix up i8/i1 issues.");
			auto ret = dynamic_cast<Codegen::ReturnStatement*>(statements[0]);
			assert(ret && "A trampoline's single statement must be a return statement.");
			statements[0] = g.CreateReturn(g.CreateTruncate(ret->GetReturnExpression(), [&](llvm::Module*) { return llvm::IntegerType::getInt1Ty(con); }));
			f = mod->getFunction(name);
		}
    } else {
		auto ty = Type(mod);
		llvm::FunctionType* t = nullptr;
		if (auto ptr = llvm::dyn_cast<llvm::PointerType>(ty)) {
			t = llvm::dyn_cast<llvm::FunctionType>(ptr->getElementType());
		} else {
			t = llvm::dyn_cast<llvm::FunctionType>(ty);
		}
        f = llvm::Function::Create(t, llvm::GlobalValue::LinkageTypes::InternalLinkage, name, mod);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(con, "entry", f);
    llvm::IRBuilder<> irbuilder(bb);
    for(auto&& x : statements)
        x->Build(irbuilder);

	if (llvm::verifyFunction(*f, llvm::VerifierFailureAction::ReturnStatusAction))
		__debugbreak();
}

Function::Function(std::function<llvm::Type*(llvm::Module*)> ret, std::string name, bool trampoline)
    : Type(ret)
    , name(std::move(name))
	, tramp(trampoline)
{}