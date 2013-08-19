#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/OverloadSet.h>

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
    out.t = a.AsLvalueType(this);
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
Expression Type::AddressOf(Expression obj, Analyzer& a) {
    // TODO: Remove this restriction, it is not very Wide.
	if (!a.IsLvalueType(obj.t))
        throw std::runtime_error("Attempted to take the address of something that was not an lvalue.");
    return Expression(a.GetPointerType(obj.t->Decay()), obj.Expr);
}

Expression Expression::BuildValue(Analyzer& a) {
    return t->BuildValue(*this, a);
}
Expression Expression::BuildAssignment(Expression rhs, Analyzer& a){
    return t->BuildAssignment(*this, rhs, a);
}
Wide::Util::optional<Expression> Expression::AccessMember(std::string name, Analyzer& a){
    return t->AccessMember(*this, std::move(name), a);
}
Expression Expression::BuildCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildCall(*this, std::move(args), a);
}
Expression Expression::BuildMetaCall(std::vector<Expression> args, Analyzer& a) {
    return t->BuildMetaCall(*this, std::move(args), a);
}
Expression Expression::BuildDereference(Analyzer& a)  {
    return t->BuildDereference(*this, a);
}
Expression Expression::BuildIncrement(bool postfix, Analyzer& a) {
    return t->BuildIncrement(*this, postfix, a);
}          
Wide::Util::optional<Expression> Expression::PointerAccessMember(std::string name, Analyzer& a) {
    return t->PointerAccessMember(*this, std::move(name), a);
}
Expression Expression::AddressOf(Analyzer& a) {
    return t->AddressOf(*this, a);
}
Codegen::Expression* Expression::BuildBooleanConversion(Analyzer& a) {
    return t->BuildBooleanConversion(*this, a);
}
Expression Expression::BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a) {
	return t->BuildBinaryExpression(*this, rhs, type, a);
}
Expression Expression::BuildNegate(Analyzer& a) {
	return t->BuildNegate(*this, a);
}
Expression Expression::BuildCall(Expression lhs, Expression rhs, Analyzer& a) {
	std::vector<Expression> args;
	args.push_back(lhs);
	args.push_back(rhs);
	return t->BuildCall(*this, std::move(args), a);
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
Wide::Util::optional<Expression> Type::AccessMember(Expression e, std::string name, Analyzer& a) {
	if (IsReference())
		return Decay()->AccessMember(e, std::move(name), a);
    if (name == "~type")
        return a.GetNothingFunctorType()->BuildValueConstruction(a);
    throw std::runtime_error("Attempted to access the member of a type that did not support it.");
}
Expression Type::BuildValue(Expression lhs, Analyzer& a) {
    if (IsComplexType())
        throw std::runtime_error("Internal Compiler Error: Attempted to build a complex type into a register.");
	if (lhs.t->IsReference())
		return Expression(lhs.t->Decay(), a.gen->CreateLoad(lhs.Expr));
    return lhs;
}            
Expression Type::BuildNegate(Expression val, Analyzer& a) {
	if (IsReference())
		return Decay()->BuildNegate(val, a);
	return Expression(a.GetBooleanType(), a.gen->CreateNegateExpression(val.BuildBooleanConversion(a)));
}

static const std::unordered_map<Lexer::TokenType, Lexer::TokenType> Assign = []() -> std::unordered_map<Lexer::TokenType, Lexer::TokenType> {
	std::unordered_map<Lexer::TokenType, Lexer::TokenType> assign;
	assign[Lexer::TokenType::LeftShift] = assign[Lexer::TokenType::LeftShiftAssign];
	assign[Lexer::TokenType::RightShift] = assign[Lexer::TokenType::RightShiftAssign];
	assign[Lexer::TokenType::Minus] = assign[Lexer::TokenType::MinusAssign];
	assign[Lexer::TokenType::Plus] = assign[Lexer::TokenType::PlusAssign];
	assign[Lexer::TokenType::Or] = assign[Lexer::TokenType::OrAssign];
	assign[Lexer::TokenType::And] = assign[Lexer::TokenType::AndAssign];
	assign[Lexer::TokenType::Xor] = assign[Lexer::TokenType::Xor];
	assign[Lexer::TokenType::Dereference] = assign[Lexer::TokenType::MulAssign];
	assign[Lexer::TokenType::Modulo] = assign[Lexer::TokenType::ModAssign];
	assign[Lexer::TokenType::Divide] = assign[Lexer::TokenType::DivAssign];
	return assign;
}();

Expression Type::BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType type, Analyzer& a) {
	if (IsReference())
		return Decay()->BuildBinaryExpression(lhs, rhs, type, a);

	// If this function is entered, it's because the type-specific logic could not resolve the operator.
	// So let us attempt ADL.
	auto ldecls = a.GetDeclContext(lhs.t->Decay()->GetDeclContext())->AccessMember(Expression(), type, a);
	auto rdecls = a.GetDeclContext(rhs.t->Decay()->GetDeclContext())->AccessMember(Expression(), type, a);
	if (ldecls) {
		if (auto loverset = dynamic_cast<OverloadSet*>(ldecls->t->Decay()))
			if (rdecls) {
				if (auto roverset = dynamic_cast<OverloadSet*>(rdecls->t->Decay()))
					return a.GetOverloadSet(loverset, roverset)->BuildValueConstruction(a).BuildCall(lhs, rhs, a);
			} else {
				return loverset->BuildValueConstruction(a).BuildCall(lhs, rhs, a);
			}
	} else
		if (rdecls)
			if (auto roverset = dynamic_cast<OverloadSet*>(rdecls->t->Decay()))
				return roverset->BuildValueConstruction(a).BuildCall(lhs, rhs, a);

	// At this point, ADL has failed to find an operator.
	// So let us attempt a default implementation for some operators.
	switch(type) {
	case Lexer::TokenType::NotEqCmp:
		return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::EqCmp, a).BuildNegate(a);
	case Lexer::TokenType::EqCmp:
		return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, a).BuildNegate(a).BuildBinaryExpression(rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a).BuildNegate(a), Lexer::TokenType::And, a);
	case Lexer::TokenType::LTE:
		return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a).BuildNegate(a);
	case Lexer::TokenType::GT:
		return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, a);
	case Lexer::TokenType::GTE:
		return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, a).BuildNegate(a);
	}
	if (Assign.find(type) != Assign.end())
		return BuildLvalueConstruction(lhs, a).BuildBinaryExpression(rhs, Assign.at(type), a);
	throw std::runtime_error("Attempted to build a binary expression; but it could not be found by the type, and a default could not be applied.");
}