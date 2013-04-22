#include "Type.h"
#include "Analyzer.h"
#include "LvalueType.h"
#include "RvalueType.h"
#include "../Codegen/Generator.h"
#include "../Codegen/Expression.h"

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)

#include <clang/AST/AST.h>

#pragma warning(pop)

clang::QualType Type::GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) {
    throw std::runtime_error("This type has no Clang representation.");
}

Expression Type::BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a) {
    Expression out;
    out.t = a.GetRvalueType(this);
    auto mem = a.gen->CreateVariable(GetLLVMType(a));
    out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a), mem);
    return out;
}

Expression Type::BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a) {
    Expression out;
    out.t = a.GetLvalueType(this);
    auto mem = a.gen->CreateVariable(GetLLVMType(a));
    out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a), mem);
    return out;
}