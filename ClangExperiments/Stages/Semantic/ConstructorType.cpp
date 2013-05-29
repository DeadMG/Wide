#include "ConstructorType.h"
#include "Analyzer.h"
#include "LvalueType.h"
#include "RvalueType.h"
#include <sstream>

#pragma warning(push, 0)

#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Expression ConstructorType::BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
    return t->BuildRvalueConstruction(std::move(args), a);
}
Expression ConstructorType::AccessMember(Expression, std::string name, Analyzer& a) {
    Expression self;
    self.t = t;
    self.Expr = nullptr;
    return t->AccessMember(self, name, a);
}

std::function<llvm::Type*(llvm::Module*)> ConstructorType::GetLLVMType(Analyzer& a) {
    std::stringstream typenam;
    typenam << this;
    auto nam = typenam.str();
    return [=](llvm::Module* mod) -> llvm::Type* {
        if (mod->getTypeByName(nam))
            return mod->getTypeByName(nam);
        return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
    };
}
Codegen::Expression* ConstructorType::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
    if (args.size() > 1)
        throw std::runtime_error("Attempt to construct a type object with too many arguments.");
    if (args.size() == 1 && args[0].t->Decay() != this)
        throw std::runtime_error("Attempt to construct a type object with something other than another instance of that type.");
    return mem;
}

Expression ConstructorType::PointerAccessMember(Expression obj, std::string name, Analyzer& a) {
    if (name == "decay") {
        return a.GetConstructorType(t->Decay())->BuildValueConstruction(std::vector<Expression>(), a);
    }
    if (name == "lvalue") {
        return a.GetConstructorType(a.GetLvalueType(t))->BuildValueConstruction(std::vector<Expression>(), a);
    }
    if (name == "rvalue") {
        return a.GetConstructorType(a.GetRvalueType(t))->BuildValueConstruction(std::vector<Expression>(), a);
    }
    throw std::runtime_error("Attempted to access the special members of a type, but the identifier provided did not name them.");
}