#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/PointerType.h>
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

    Expression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Analyzer& a) {
        if (args.size() == 0)
            throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
        if (args[0].t->Decay() != a.GetPointerType(t))
            throw std::runtime_error("Attempted to emplace a T into a type that was not a pointer to T.");
        auto expr = args.front();
        args.erase(args.begin());
        return ConcreteExpression(a.GetRvalueType(t), a.gen->CreateChainExpression(t->BuildInplaceConstruction(expr.Expr, args, a), expr.Expr));
    }
};

Expression ConstructorType::BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    return t->BuildRvalueConstruction(std::move(args), a);
}
Wide::Util::optional<ConcreteExpression> ConstructorType::AccessMember(ConcreteExpression, std::string name, Analyzer& a) {
    ConcreteExpression self;
    self.t = t;
    self.Expr = nullptr;
    return t->AccessMember(self, name, a);
}

Wide::Util::optional<ConcreteExpression> ConstructorType::PointerAccessMember(ConcreteExpression obj, std::string name, Analyzer& a) {
    if (name == "decay")
        return a.GetConstructorType(t->Decay())->BuildValueConstruction(std::vector<ConcreteExpression>(), a);
    if (name == "lvalue")
        return a.GetConstructorType(a.AsLvalueType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a);
    if (name == "rvalue")
        return a.GetConstructorType(a.AsRvalueType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a);
    if (name == "pointer")
        return a.GetConstructorType(a.GetPointerType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a);
    if (name == "size")
        return ConcreteExpression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->size(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
    if (name == "alignment")
        return ConcreteExpression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->alignment(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
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