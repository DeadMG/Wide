#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Reference.h>
#include <sstream>

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

struct EmplaceType : public MetaType {
    EmplaceType(Type* con)
        : t(con) {}
    Type* t;

    ConcreteExpression BuildCall(ConcreteExpression obj, std::vector<ConcreteExpression> args, Context c) override final {
        if (args.size() == 0)
            throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
        if (args[0].t->Decay() != c->GetPointerType(t))
            throw std::runtime_error("Attempted to emplace a T into a type that was not a pointer to T.");
        auto expr = args.front();
        args.erase(args.begin());
        return ConcreteExpression(c->GetRvalueType(t), c->gen->CreateChainExpression(t->BuildInplaceConstruction(expr.Expr, args, c), expr.Expr));
    }
};

ConcreteExpression ConstructorType::BuildCall(ConcreteExpression self, std::vector<ConcreteExpression> args, Context c) {
    assert(self.t->Decay() == this);
    return t->BuildRvalueConstruction(std::move(args), c);
}
Wide::Util::optional<ConcreteExpression> ConstructorType::AccessMember(ConcreteExpression self, std::string name, Context c) {
    assert(self.t->Decay() == this);
    //return t->AccessStaticMember(name, c);
    if (name == "decay")
        return c->GetConstructorType(t->Decay())->BuildValueConstruction({}, c);
    if (name == "lvalue")
        return c->GetConstructorType(c->GetLvalueType(t))->BuildValueConstruction({}, c);
    if (name == "rvalue")
        return c->GetConstructorType(c->GetRvalueType(t))->BuildValueConstruction({}, c);
    if (name == "pointer")
        return c->GetConstructorType(c->GetPointerType(t))->BuildValueConstruction({}, c);
    if (name == "size")
        return ConcreteExpression(c->GetIntegralType(64, false), c->gen->CreateIntegralExpression(t->size(*c), false, c->GetIntegralType(64, false)->GetLLVMType(*c)));
    if (name == "alignment")
        return ConcreteExpression(c->GetIntegralType(64, false), c->gen->CreateIntegralExpression(t->alignment(*c), false, c->GetIntegralType(64, false)->GetLLVMType(*c)));
    if (name == "emplace") {
        if (!emplace) emplace = c->arena.Allocate<EmplaceType>(t);
        return emplace->BuildValueConstruction({}, c);
    }
    throw std::runtime_error("Attempted to access the special members of a type, but the identifier provided did not name a special member.");
}

ConstructorType::ConstructorType(Type* con) {
    t = con;
    emplace = nullptr;
}