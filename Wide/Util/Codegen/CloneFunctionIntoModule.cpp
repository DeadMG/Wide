#include <Wide/Util/Codegen/CloneFunctionIntoModule.h>

#pragma warning(push, 0)
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Transforms/Utils/Cloning.h>
#pragma warning(pop)

llvm::Function* Wide::Util::CloneFunctionIntoModule(llvm::Function* src, llvm::Module* destModule)
{
    auto dest = llvm::Function::Create(llvm::cast<llvm::FunctionType>(src->getType()->getElementType()), src->getLinkage(), src->getName(), destModule);
    llvm::ValueToValueMapTy valuemap;
    llvm::SmallVector<llvm::ReturnInst*, 8> returns;
    llvm::CloneFunctionInto(dest, src, valuemap, true, returns);
    return dest;
}