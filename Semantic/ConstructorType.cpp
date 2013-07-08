#include <Semantic/ConstructorType.h>
#include <Semantic/Analyzer.h>
#include <Semantic/Reference.h>
#include <Semantic/IntegralType.h>
#include <Codegen/Generator.h>
#include <Semantic/PointerType.h>
#include <sstream>

#pragma warning(push, 0)

#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>

#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

struct EmplaceType : public MetaType {
    EmplaceType(Type* con)
        : t(con) {}
    Type* t;

    Expression BuildCall(Expression obj, std::vector<Expression> args, Analyzer& a) {
        if (args.size() == 0)
            throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
        if (args[0].t->Decay() != a.GetPointerType(t))
            throw std::runtime_error("Attempted to emplace a T into a type that was not a pointer to T.");
        auto expr = args.front();
        args.erase(args.begin());
        return Expression(a.GetRvalueType(t), a.gen->CreateChainExpression(t->BuildInplaceConstruction(expr.Expr, args, a), expr.Expr));
    }

};

Expression ConstructorType::BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
    return t->BuildRvalueConstruction(std::move(args), a);
}
Expression ConstructorType::AccessMember(Expression, std::string name, Analyzer& a) {
    Expression self;
    self.t = t;
    self.Expr = nullptr;
    return t->AccessMember(self, name, a);
}

Expression ConstructorType::PointerAccessMember(Expression obj, std::string name, Analyzer& a) {
    if (name == "decay")
        return a.GetConstructorType(t->Decay())->BuildValueConstruction(std::vector<Expression>(), a);
    if (name == "lvalue")
        return a.GetConstructorType(a.GetLvalueType(t))->BuildValueConstruction(std::vector<Expression>(), a);
    if (name == "rvalue")
        return a.GetConstructorType(a.GetRvalueType(t))->BuildValueConstruction(std::vector<Expression>(), a);
    if (name == "pointer")
        return a.GetConstructorType(a.GetPointerType(t))->BuildValueConstruction(std::vector<Expression>(), a);
    if (name == "size")
        return Expression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->size(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
    if (name == "alignment")
        return Expression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->alignment(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
    if (name == "emplace") {
        if (!emplace) emplace = a.arena.Allocate<EmplaceType>(t);
        return emplace->BuildValueConstruction(a);
    }
    throw std::runtime_error("Attempted to access the special members of a type, but the identifier provided did not name a special member.");
}
ConstructorType::ConstructorType(Type* con) {
    t = con;
    emplace = nullptr;
}