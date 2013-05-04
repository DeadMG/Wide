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

Expression Type::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (IsComplexType())
        throw std::runtime_error("Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = a.gen->CreateVariable(GetLLVMType(a));
    return Expression(this, a.gen->CreateLoad(a.gen->CreateChainExpression(BuildInplaceConstruction(mem, std::move(args), a), mem)));
}
ConversionRank Type::RankConversionFrom(Type* from, Analyzer& a) {
    // We only cover the following cases:
    // U to T         - convertible for any U
    // T& to T        - copyable
    // T&& to T       - movable
    // "this" is always the to type. We want to know if we can convert from "from" to "this".

    // U to this&& is just U to this, then this to this&&. T to T&& is always valid- even for something like std::mutex.

    // The default is not convertible but movable and copyable.
    if (from->IsReference(this))
        return ConversionRank::Zero;
    return ConversionRank::None;
}