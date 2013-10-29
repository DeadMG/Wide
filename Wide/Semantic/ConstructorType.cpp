#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Reference.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

struct EmplaceType : public MetaType {
    EmplaceType(Type* con)
        : t(con) {}
    Type* t;

    Expression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override {
        if (args.size() == 0)
            throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
        if (args[0].t->Decay() != a.GetPointerType(t))
            throw std::runtime_error("Attempted to emplace a T into a type that was not a pointer to T.");
        auto expr = args.front();
        args.erase(args.begin());
        return ConcreteExpression(a.GetRvalueType(t), a.gen->CreateChainExpression(t->BuildInplaceConstruction(expr.Expr, args, a, where), expr.Expr));
    }
};

Expression ConstructorType::BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) {
    return t->BuildRvalueConstruction(std::move(args), a, where);
}
Wide::Util::optional<ConcreteExpression> ConstructorType::AccessMember(ConcreteExpression, std::string name, Analyzer& a, Lexer::Range where) {
    ConcreteExpression self;
    self.t = t;
    self.Expr = nullptr;
    return t->AccessMember(self, name, a, where);
}

Wide::Util::optional<ConcreteExpression> ConstructorType::PointerAccessMember(ConcreteExpression obj, std::string name, Analyzer& a, Lexer::Range where) {
    if (name == "decay")
        return a.GetConstructorType(t->Decay())->BuildValueConstruction(std::vector<ConcreteExpression>(), a, where);
    if (name == "lvalue")
        return a.GetConstructorType(a.GetLvalueType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a, where);
    if (name == "rvalue")
        return a.GetConstructorType(a.GetRvalueType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a, where);
    if (name == "pointer")
        return a.GetConstructorType(a.GetPointerType(t))->BuildValueConstruction(std::vector<ConcreteExpression>(), a, where);
    if (name == "size")
        return ConcreteExpression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->size(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
    if (name == "alignment")
        return ConcreteExpression(a.GetIntegralType(64, false), a.gen->CreateIntegralExpression(t->alignment(a), false, a.GetIntegralType(64, false)->GetLLVMType(a)));
    if (name == "emplace") {
        if (!emplace) emplace = a.arena.Allocate<EmplaceType>(t);
        return emplace->BuildValueConstruction(a, where);
    }
    throw std::runtime_error("Attempted to access the special members of a type, but the identifier provided did not name a special member.");
}
ConstructorType::ConstructorType(Type* con) {
    t = con;
    emplace = nullptr;
}