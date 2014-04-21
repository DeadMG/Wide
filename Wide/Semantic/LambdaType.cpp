#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>
#include <sstream>
#include <Wide/Parser/AST.h>

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

std::vector<Type*> GetTypesFrom(std::vector<std::pair<std::string, Type*>>& vec) {
    std::vector<Type*> out;
    for (auto cap : vec)
        out.push_back(cap.second);
    return out;
}
LambdaType::LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const AST::Lambda* l, Analyzer& a)
    : contents(GetTypesFrom(capturetypes)), lam(l) 
{
    std::size_t i = 0;
    for (auto pair : capturetypes)
        names[pair.first] = i++;
}

ConcreteExpression LambdaType::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    if (val.t == this)
        val = BuildLvalueConstruction({ val }, c);
    args.insert(args.begin(), val);
    std::vector<Type*> types;
    for (auto arg : args)
        types.push_back(arg.t);
    auto overset = c->GetOverloadSet(c->GetCallableForFunction(lam, val.t, GetNameForOperator(Lexer::TokenType::OpenBracket)));
    auto call = overset->Resolve(types, *c, c.source);
    if (!call) overset->IssueResolutionError(types, c);
    return call->Call(args, c);
}
ConcreteExpression LambdaType::BuildLambdaFromCaptures(std::vector<ConcreteExpression> exprs, Context c) {
    auto memory = ConcreteExpression(c->GetLvalueType(this), c->gen->CreateVariable(GetLLVMType(*c), alignment(*c)));
    if (GetContents().size() == 0)
        return memory;
    Codegen::Expression* construct = 0;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(c->GetLvalueType(GetContents()[i]));
        types.push_back(exprs[i].t);
        auto conset = GetContents()[i]->GetConstructorOverloadSet(*c, Lexer::Access::Public);
        auto call = conset->Resolve(types, *c, c.source);
        if (!call) conset->IssueResolutionError(types, c);
        auto expr = call->Call({ PrimitiveAccessMember(memory, i, *c), exprs[i] }, c).Expr;
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
std::string LambdaType::explain(Analyzer& a) {
    std::stringstream strstream;
    strstream << this;
    return "(lambda instantiation " + strstream.str() + " at location " + lam->location + ")";
}