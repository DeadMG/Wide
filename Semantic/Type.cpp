#include <Semantic/Type.h>
#include <Semantic/Analyzer.h>
#include <Semantic/Reference.h>
#include <Semantic/PointerType.h>
#include <Codegen/Generator.h>
#include <Codegen/Expression.h>

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
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].t->BuildValue(args[0], a);
        out.Expr = a.gen->CreateChainExpression(a.gen->CreateStore(mem, args[0].Expr), mem);
    } else
        out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a), mem);
    out.steal = true;
    return out;
}

Expression Type::BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a) {
    Expression out;
    out.t = a.GetLvalueType(this);
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
    if (!IsComplexType() && args.size() == 1 && args[0].t->Decay() == this) {
        args[0] = args[0].t->BuildValue(args[0], a);
        out.Expr = a.gen->CreateChainExpression(a.gen->CreateStore(mem, args[0].Expr), mem);
    } else
        out.Expr = a.gen->CreateChainExpression(BuildInplaceConstruction(mem, args, a), mem);    return out;
}

Expression Type::BuildValueConstruction(std::vector<Expression> args, Analyzer& a) {
    if (IsComplexType())
        throw std::runtime_error("Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = a.gen->CreateVariable(GetLLVMType(a), alignment(a));
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
Expression Type::BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
    auto expr = lhs.t->BuildEQComparison(lhs, rhs, a);
    if (expr.t != a.GetBooleanType())
        throw std::runtime_error("Cannot automatically implement ~= on top of an == that does not return a boolean.");
    expr.Expr = a.gen->CreateNegateExpression(expr.Expr);
    return expr;
}
Expression Type::AddressOf(Expression obj, Analyzer& a) {
    // TODO: Remove this restriction, it is not very Wide.
    if (!dynamic_cast<LvalueType*>(obj.t))
        throw std::runtime_error("Attempted to take the address of something that was not an lvalue.");
    return Expression(a.GetPointerType(obj.t->Decay()), obj.Expr);
}

Expression Expression::BuildValue(Analyzer& a) {
    return t->BuildValue(*this, a);
}
Expression Expression::BuildRightShift(Expression rhs, Analyzer& a) {
    return t->BuildRightShift(*this, rhs, a);
}
Expression Expression::BuildLeftShift(Expression rhs, Analyzer& a) {
    return t->BuildLeftShift(*this, rhs, a);
}
Expression Expression::BuildAssignment(Expression rhs, Analyzer& a){
    return t->BuildAssignment(*this, rhs, a);
}
Expression Expression::AccessMember(std::string name, Analyzer& a){
    return t->AccessMember(*this, std::move(name), a);
}
Expression Expression::BuildCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildCall(*this, std::move(args), a);
}
Expression Expression::BuildMetaCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildMetaCall(*this, std::move(args), a);
}
Expression Expression::BuildEQComparison(Expression rhs, Analyzer& a) {
    return t->BuildEQComparison(*this, rhs, a);
}
Expression Expression::BuildNEComparison(Expression rhs, Analyzer& a) {
    return t->BuildNEComparison(*this, rhs, a);
}
Expression Expression::BuildLTComparison(Expression rhs, Analyzer& a) {
    return t->BuildLTComparison(*this, rhs, a);
}
Expression Expression::BuildLTEComparison(Expression rhs, Analyzer& a) {
    return t->BuildLTEComparison(*this, rhs, a);
}
Expression Expression::BuildGTComparison(Expression rhs, Analyzer& a) {
    return t->BuildGTComparison(*this, rhs, a);
}
Expression Expression::BuildGTEComparison(Expression rhs, Analyzer& a) {
    return t->BuildGTEComparison(*this, rhs, a);
}
Expression Expression::BuildDereference(Analyzer& a)  {
    return t->BuildDereference(*this, a);
}
Expression Expression::BuildOr(Expression rhs, Analyzer& a) {
    return t->BuildOr(*this, rhs, a);
}
Expression Expression::BuildAnd(Expression rhs, Analyzer& a) {
    return t->BuildAnd(*this, rhs, a);
}          
Expression Expression::BuildMultiply(Expression rhs, Analyzer& a) {
    return t->BuildMultiply(*this, rhs, a);
}          
Expression Expression::BuildPlus(Expression rhs, Analyzer& a) {
    return t->BuildPlus(*this, rhs, a);
}          
Expression Expression::BuildIncrement(bool postfix, Analyzer& a) {
    return t->BuildIncrement(*this, postfix, a);
}          
Expression Expression::PointerAccessMember(std::string name, Analyzer& a) {
    return t->PointerAccessMember(*this, std::move(name), a);
}
Expression Expression::AddressOf(Analyzer& a) {
    return t->AddressOf(*this, a);
}
Codegen::Expression* Expression::BuildBooleanConversion(Analyzer& a) {
    return t->BuildBooleanConversion(*this, a);
}

Expression Type::BuildValueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildValueConstruction(std::move(args), a);
}
Expression Type::BuildRvalueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildRvalueConstruction(std::move(args), a);
}
Expression Type::BuildLvalueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildLvalueConstruction(std::move(args), a);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Expression arg, Analyzer& a){
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildInplaceConstruction(mem, std::move(args), a);
}
Expression Expression::BuildCall(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildCall(std::move(args), a);
}
Expression Type::BuildValueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildValueConstruction(args, a);
}
Expression Type::BuildRvalueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildRvalueConstruction(args, a);
}
Expression Type::BuildLvalueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildLvalueConstruction(args, a);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Analyzer& a) {
    std::vector<Expression> args;
    return BuildInplaceConstruction(mem, args, a);
}
Expression Expression::BuildCall(Analyzer& a) {
    std::vector<Expression> args;
    return BuildCall(args, a);
}
Expression Type::AccessMember(Expression, std::string name, Analyzer& a) {
    if (name == "~type")
        return a.GetNothingFunctorType()->BuildValueConstruction(a);
    throw std::runtime_error("Attempted to access the member of a type that did not support it.");
}
/*
Expression Expression::BuildValue(Analyzer& a) {
    return t->BuildValue(*this, a);
}
Expression Expression::BuildRightShift(Expression rhs, Analyzer& a) {
    return t->BuildRightShift(*this, rhs, a);
}
Expression Expression::BuildLeftShift(Expression rhs, Analyzer& a) {
    return t->BuildLeftShift(*this, rhs, a);
}
Expression Expression::BuildAssignment(Expression rhs, Analyzer& a){
    return t->BuildAssignment(*this, rhs, a);
}
Expression Expression::AccessMember(std::string name, Analyzer& a){
    return t->AccessMember(*this, std::move(name), a);
}
Expression Expression::BuildCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildCall(*this, std::move(args), a);
}
Expression Expression::BuildMetaCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildMetaCall(*this, std::move(args), a);
}
Expression Expression::BuildEQComparison(Expression rhs, Analyzer& a) {
    return t->BuildEQComparison(*this, rhs, a);
}
Expression Expression::BuildNEComparison(Expression rhs, Analyzer& a) {
    return t->BuildNEComparison(*this, rhs, a);
}
Expression Expression::BuildLTComparison(Expression rhs, Analyzer& a) {
    return t->BuildLTComparison(*this, rhs, a);
}
Expression Expression::BuildLTEComparison(Expression rhs, Analyzer& a) {
    return t->BuildLTEComparison(*this, rhs, a);
}
Expression Expression::BuildGTComparison(Expression rhs, Analyzer& a) {
    return t->BuildGTComparison(*this, rhs, a);
}
Expression Expression::BuildGTEComparison(Expression rhs, Analyzer& a) {
    return t->BuildGTEComparison(*this, rhs, a);
}
Expression Expression::BuildDereference(Analyzer& a)  {
    return t->BuildDereference(*this, a);
}
Expression Expression::BuildOr(Expression rhs, Analyzer& a) {
    return t->BuildOr(*this, rhs, a);
}
Expression Expression::BuildAnd(Expression rhs, Analyzer& a) {
    return t->BuildAnd(*this, rhs, a);
}          
Expression Expression::BuildMultiply(Expression rhs, Analyzer& a) {
    return t->BuildMultiply(*this, rhs, a);
}          
Expression Expression::BuildPlus(Expression rhs, Analyzer& a) {
    return t->BuildPlus(*this, rhs, a);
}          
Expression Expression::BuildIncrement(bool postfix, Analyzer& a) {
    return t->BuildIncrement(*this, postfix, a);
}          
Expression Expression::PointerAccessMember(std::string name, Analyzer& a) {
    return t->PointerAccessMember(*this, std::move(name), a);
}
Expression Expression::AddressOf(Analyzer& a) {
    return t->AddressOf(*this, a);
}
Codegen::Expression* Expression::BuildBooleanConversion(Analyzer& a) {
    return t->BuildBooleanConversion(*this, a);
}

Expression Type::BuildValueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildValueConstruction(std::move(args), a);
}
Expression Type::BuildRvalueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildRvalueConstruction(std::move(args), a);
}
Expression Type::BuildLvalueConstruction(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildLvalueConstruction(std::move(args), a);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Expression arg, Analyzer& a){
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildInplaceConstruction(mem, std::move(args), a);
}
Expression Expression::BuildCall(Expression arg, Analyzer& a) {
    std::vector<Expression> args;
    args.push_back(arg);
    return BuildCall(std::move(args), a);
}
Expression Type::BuildValueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildValueConstruction(args, a);
}
Expression Type::BuildRvalueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildRvalueConstruction(args, a);
}
Expression Type::BuildLvalueConstruction(Analyzer& a) {
    std::vector<Expression> args;
    return BuildLvalueConstruction(args, a);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Analyzer& a) {
    std::vector<Expression> args;
    return BuildInplaceConstruction(mem, args, a);
}
Expression Expression::BuildCall(Analyzer& a) {
    std::vector<Expression> args;
    return BuildCall(args, a);
}*/