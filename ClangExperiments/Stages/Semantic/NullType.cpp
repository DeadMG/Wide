#include "NullType.h"
#include "ClangTU.h"
#include "Analyzer.h"
#include "../Codegen/Generator.h"
#include <sstream>
#include <string>

#pragma warning(push, 0)
#include <clang/AST/ASTContext.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

clang::QualType NullType::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    return TU.GetASTContext().NullPtrTy;
}
std::function<llvm::Type*(llvm::Module*)> NullType::GetLLVMType(Analyzer& a) {
    std::stringstream name;
    name << "struct.__" << this;
    auto nam = name.str();
    return [=, &a](llvm::Module* mod) -> llvm::Type* {
        if (mod->getTypeByName(nam))
            return mod->getTypeByName(nam);
        auto t = llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
        a.gen->AddEliminateType(t);
        return t;
    };
}
std::size_t NullType::size(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getTypeAllocSize(llvm::IntegerType::getInt8Ty(a.gen->context));
}
std::size_t NullType::alignment(Analyzer& a) {
    return llvm::DataLayout(a.gen->main.getDataLayout()).getABIIntegerTypeAlignment(8);;
}