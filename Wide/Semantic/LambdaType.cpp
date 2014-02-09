#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

std::vector<Type*> GetTypesFrom(std::vector<std::pair<std::string, Type*>>& vec) {
    std::vector<Type*> out;
    for (auto cap : vec)
        out.push_back(cap.second);
    return out;
}
LambdaType::LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const AST::Lambda* l, Analyzer& a)
    : AggregateType(GetTypesFrom(capturetypes), a), lam(l) 
{
    std::size_t i = 0;
    for (auto pair : capturetypes)
        names[pair.first] = i++;
}

ConcreteExpression LambdaType::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    if (val.t == this)
        val = BuildLvalueConstruction(val, c);
    args.insert(args.begin(), val);
    std::vector<Type*> types;
    for (auto arg : args)
        types.push_back(arg.t);
    auto call = c->GetOverloadSet(c->GetCallableForFunction(lam, val.t))->Resolve(types, *c);
    if (!call)
        throw std::runtime_error("Attempted to call a lambda type but overload resolution could not resolve the call.");
    return call->Call(args, c);
}
ConcreteExpression LambdaType::BuildLambdaFromCaptures(std::vector<ConcreteExpression> exprs, Context c) {
    auto memory = ConcreteExpression(c->GetLvalueType(this), c->gen->CreateVariable(GetLLVMType(*c), alignment(*c)));
    if (GetMembers().size() == 0)
        return memory;
    Codegen::Expression* construct = 0;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(c->GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i].t);
        auto call = GetMembers()[i]->GetConstructorOverloadSet(*c)->Resolve(types, *c);
        if (!call)
            throw std::runtime_error("Could not construct lambda from literal.");
        auto expr = call->Call(PrimitiveAccessMember(memory, i, *c), exprs[i], c).Expr;
        construct = construct ? c->gen->CreateChainExpression(construct, expr) : expr;
    }
    memory.t = c->GetRvalueType(this);
    memory.Expr = c->gen->CreateChainExpression(construct, memory.Expr);
    memory.steal = true;
    return memory;
}

Wide::Util::optional<ConcreteExpression> LambdaType::LookupCapture(ConcreteExpression self, std::string name, Context c) {
    if (names.find(name) != names.end())
        return PrimitiveAccessMember(self, names[name], *c);
    return Wide::Util::none;
}