// Silly name because MSVC can't cope with same-name CPP files even when in a different folder.

#include <Wide/Codegen/Function.h>
#include <Wide/Codegen/Statement.h>
#include <Wide/Codegen/LLVMGenerator.h>

using namespace Wide;
using namespace LLVMCodegen;

#pragma warning(push, 0)

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/Verifier.h>

#pragma warning(pop)

llvm::Value* Function::GetParameter(unsigned i) {
    return ParameterValues[i];
}
void Function::EmitCode(llvm::Module* mod, llvm::LLVMContext& con, Generator& g) {
    llvm::Function* f = nullptr;
    if ((f = mod->getFunction(name))) {
        if (f->getType() == Type(mod))
            f = mod->getFunction(name);
        else {
            auto fty = llvm::dyn_cast<llvm::FunctionType>(f->getType()->getElementType());
            auto ty = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(Type(mod))->getElementType());
            
            // Currently do not deal with byval, return coercion.
            // If an i8/i1 mismatch, and we are a trampoline, just fix up the return statement and change the type.
            if (tramp && fty->getReturnType() == llvm::IntegerType::getInt1Ty(con) && ty->getReturnType() == llvm::IntegerType::getInt8Ty(con)) {
                // Expect one return statement in a trampoline. Might modify if cause to later.
                assert(statements.size() == 1 && "A trampoline must have only one return statement so that the codegen can fix up i8/i1 issues.");
                auto ret = dynamic_cast<LLVMCodegen::ReturnStatement*>(statements[0]);
                assert(ret && "A trampoline's single statement must be a return statement.");
                statements[0] = g.CreateReturn(g.CreateTruncate(ret->GetReturnExpression(), [&](llvm::Module*) { return llvm::IntegerType::getInt1Ty(con); }));
            } else {
                throw std::runtime_error("Found a function of the same name in the module but it had the wrong LLVM type.");
            }

            f = mod->getFunction(name);
            
        }
    } else {
        auto linkage = tramp ? llvm::GlobalValue::LinkageTypes::ExternalLinkage : llvm::GlobalValue::LinkageTypes::InternalLinkage;
        auto t = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(Type(mod))->getElementType());
        f = llvm::Function::Create(t, linkage, name, mod);
    }

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(con, "entry", f);
    llvm::IRBuilder<> irbuilder(bb);
    
    auto fty = llvm::dyn_cast<llvm::FunctionType>(f->getType()->getElementType());
    auto ty = llvm::dyn_cast<llvm::FunctionType>(llvm::dyn_cast<llvm::PointerType>(Type(mod))->getElementType());

    // Always in sync except when Clang skips an empty type parameter.
    auto arg_begin = f->getArgumentList().begin();
    for(std::size_t i = 0; i < ty->getNumParams(); ++i) {
        if (ty->getParamType(i) == arg_begin->getType()) {
            ParameterValues[i] = arg_begin;
            ++arg_begin;
            continue;
        }
        if (auto ptr = llvm::dyn_cast<llvm::PointerType>(ty->getParamType(i))) {
            auto el = ptr->getElementType();
            if (g.IsEliminateType(el)) {
                ParameterValues[i] = llvm::Constant::getNullValue(ty->getParamType(i));
                continue;
            }
        }
        if (ty->getParamType(i) == llvm::IntegerType::getInt8Ty(con) && arg_begin->getType() == llvm::IntegerType::getInt1Ty(con)) {
            ParameterValues[i] = irbuilder.CreateZExt(arg_begin, llvm::IntegerType::getInt1Ty(con));
            ++arg_begin;
            continue;
        }
        assert(false && "The function type did not match the expected type and none of the compensation schemes were successful in resolving the mismatch.");
    }

    g.TieFunction(f, this);

    for(auto&& x : statements)
        x ? x->Build(irbuilder, g) : void();

    if (llvm::verifyFunction(*f, llvm::VerifierFailureAction::PrintMessageAction))
        throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
}

Function::Function(std::function<llvm::Type*(llvm::Module*)> ret, std::string name, Semantic::Function* de,  bool trampoline)
    : Type(ret)
    , name(std::move(name))
    , tramp(trampoline)
    , debug(de)
{}
void Function::AddStatement(Codegen::Statement* s) {
    auto p = dynamic_cast<LLVMCodegen::Statement*>(s);
    assert(p);
    statements.push_back(p);
}