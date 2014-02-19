#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

TupleType::TupleType(std::vector<Type*> types, Analyzer& a)
: AggregateType(types, a) {}

ConcreteExpression TupleType::ConstructFromLiteral(std::vector<ConcreteExpression> exprs, Context c) {
    assert(exprs.size() == GetMembers().size());
    for (std::size_t i = 0; i < exprs.size(); ++i)
        assert(GetMembers()[i] == exprs[i].t->Decay());

    auto memory = ConcreteExpression(c->GetLvalueType(this), c->gen->CreateVariable(GetLLVMType(*c), alignment(*c)));
    if (GetMembers().size() == 0)
        return memory;
    Codegen::Expression* construct = 0;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(c->GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i].t);
        auto call = GetMembers()[i]->GetConstructorOverloadSet(*c, Lexer::Access::Public)->Resolve(types, *c, this);
        if (!call)
            throw std::runtime_error("Could not construct tuple value from literal.");
        auto expr = call->Call({ PrimitiveAccessMember(memory, i, *c), exprs[i] }, c).Expr;
        construct = construct ? c->gen->CreateChainExpression(construct, expr) : expr;
    }
    memory.t = c->GetRvalueType(this);
    memory.Expr = c->gen->CreateChainExpression(construct, memory.Expr);
    memory.steal = true;
    return memory;
}

bool TupleType::IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) {
    auto udt = dynamic_cast<TupleInitializable*>(other);
    if (!udt) return Type::IsA(self, other, a, access);
    auto udt_members = udt->GetTypesForTuple(a);
    if (!udt_members) return false;
    if (GetMembers().size() != udt_members->size()) return false;
    bool is = true;
    for (std::size_t i = 0; i < GetMembers().size(); ++i) {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(udt_members->at(i)));
        types.push_back(IsLvalueType(self) ? a.GetLvalueType(GetMembers()[i]) : a.GetRvalueType(GetMembers()[i]));
        is = is && udt_members->at(i)->GetConstructorOverloadSet(a, Lexer::Access::Public)->Resolve(types, a, this);
    }
    return is;
}