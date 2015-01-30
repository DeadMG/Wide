#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <sstream>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

std::vector<Type*> GetTypesFrom(std::vector<std::pair<Parse::Name, Type*>>& vec) {
    std::vector<Type*> out;
    for (auto cap : vec)
        out.push_back(cap.second);
    return out;
}
LambdaType::LambdaType(std::vector<std::pair<Parse::Name, Type*>> capturetypes, const Parse::Lambda* l, Type* con, Analyzer& a)
: contents(GetTypesFrom(capturetypes)), lam(l), AggregateType(a), context(con)
{
    std::size_t i = 0;
    for (auto pair : capturetypes)
        names[pair.first] = i++;
}

std::shared_ptr<Expression> LambdaType::ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType(key));
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(lam, args[0]->GetType(key), "operator()", args[0]->GetType(key), [=](Wide::Parse::Name name, Lexer::Range where) {
        return Type::AccessMember(args[0], name, { this, where }); 
    }));
    auto call = overset->Resolve(types, c.from);
    if (!call) overset->IssueResolutionError(types, c);
    return call->Call(key, std::move(args), c);
}
std::shared_ptr<Expression> LambdaType::BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    auto self = CreateTemporary(this, c);
    if (contents.size() == 0)
        return std::make_shared<ImplicitLoadExpr>(std::move(self));
    return CreateResultExpression([=](Expression::InstanceKey f) {
        std::vector<std::shared_ptr<Expression>> initializers;
        for (std::size_t i = 0; i < exprs.size(); ++i) {
            std::vector<Type*> types;
            types.push_back(analyzer.GetLvalueType(GetMembers()[i]));
            types.push_back(exprs[i]->GetType(f));
            auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetMembers()[i]));
            auto call = conset->Resolve(types, c.from);
            if (!call) conset->IssueResolutionError(types, c);
            // Don't PrimAccessMember because it collapses references, and DO NOT WANT
            auto obj = CreatePrimUnOp(self, types[0], [this, i](llvm::Value* val, CodegenContext& con) {
                return con.CreateStructGEP(val, boost::get<LLVMFieldIndex>(GetLocation(i)).index);
            });
            initializers.push_back(call->Call(f, { std::move(obj), std::move(exprs[i]) }, c));
        }

        auto destructor = BuildDestructorCall(f, self, c, true);
        return CreatePrimGlobal(this, [=](CodegenContext& con) -> llvm::Value* {
            for (auto&& init : initializers)
                init->GetValue(con);
            if (AlwaysKeepInMemory(con)) {
                if (IsTriviallyDestructible())
                    con.AddDestructor(destructor);
                return self->GetValue(con);
            }
            return con->CreateLoad(self->GetValue(con));
        });
    });
}

std::shared_ptr<Expression> LambdaType::LookupCapture(std::shared_ptr<Expression> self, Parse::Name name) {
    if (names.find(name) != names.end())
        return PrimitiveAccessMember(std::move(self), names[name]);
    return nullptr;
}
std::string LambdaType::explain() {
    std::stringstream strstream;
    strstream << this;
    return "(lambda instantiation " + strstream.str() + " at location " + lam->location + ")";
}