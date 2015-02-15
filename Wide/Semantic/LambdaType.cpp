#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/FunctionSkeleton.h>
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
LambdaType::LambdaType(std::vector<std::pair<Parse::Name, Type*>> capturetypes, FunctionSkeleton* skel, Analyzer& a)
: contents(GetTypesFrom(capturetypes)), skeleton(skel), AggregateType(a)
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
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(skeleton));
    auto call = overset->Resolve(types, c.from);
    if (!call) overset->IssueResolutionError(types, c);
    return call->Call(key, std::move(args), c);
}
std::shared_ptr<Expression> LambdaType::BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    auto self = CreateTemporary(this, c);
    if (contents.size() == 0)
        return std::make_shared<ImplicitLoadExpr>(std::move(self));
    return CreateResultExpression(Range::Elements(self) | Range::Concat(Range::Container(exprs)), [=](Expression::InstanceKey f) {
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
        return CreatePrimGlobal(Range::Container(initializers), this, [=](CodegenContext& con) -> llvm::Value* {
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
    auto lam = dynamic_cast<const Parse::Lambda*>(skeleton->GetASTFunction());
    return "(lambda instantiation " + strstream.str() + " at location " + lam->location + ")";
}
Type* LambdaType::GetContext() {
    return skeleton->GetContext();
}