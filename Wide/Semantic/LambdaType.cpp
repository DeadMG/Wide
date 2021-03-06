#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Functions/FunctionSkeleton.h>
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
LambdaType::LambdaType(const Parse::Lambda* lam, std::vector<std::pair<Parse::Name, Type*>> types, Location l, Analyzer& a)
: AggregateType(a, l), lam(lam), contents(GetTypesFrom(types))
{
    std::size_t i = 0;
    for (auto pair : types)
        names[pair.first] = i++;
}

std::shared_ptr<Expression> LambdaType::ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(lam, Location(l, this)));
    auto call = overset->Resolve(types, c.from);
    if (!call) return overset->IssueResolutionError(types, c);
    return call->Call(std::move(args), c);
}
std::shared_ptr<Expression> LambdaType::BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    auto self = CreateTemporary(this, c);
    if (contents.size() == 0)
        return std::make_shared<ImplicitLoadExpr>(std::move(self));
    std::vector<std::shared_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetMembers()[i]->GetAccess(c.from));
        auto call = conset->Resolve(types, c.from);
        if (!call) return conset->IssueResolutionError(types, c);
        // Don't PrimAccessMember because it collapses references, and DO NOT WANT
        auto obj = CreatePrimUnOp(self, types[0], [this, i](llvm::Value* val, CodegenContext& con) {
            return con.CreateStructGEP(val, boost::get<LLVMFieldIndex>(GetLocation(i)).index);
        });
        initializers.push_back(call->Call({ std::move(obj), std::move(exprs[i]) }, c));
    }

    auto destructor = BuildDestructorCall(self, c, true);
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

void LambdaType::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::Lambda>(a.LambdaCaptureAnalyzers, [](const Parse::Lambda* l, Analyzer& a, std::unordered_set<Parse::Name>& local_names) {
        std::unordered_set<Parse::Name> implicit_caps;
        std::unordered_set<Parse::Name> new_local_names;
        for (auto&& cap : l->Captures)
            for (auto&& name : cap.name)
                new_local_names.insert(name.name);
        for (auto&& arg : l->args)
            new_local_names.insert(arg.name);
        for (auto&& stmt : l->statements) {
            if (a.LambdaCaptureAnalyzers.find(typeid(*stmt)) == a.LambdaCaptureAnalyzers.end())
                continue;
            auto nested_caps = a.LambdaCaptureAnalyzers[typeid(*stmt)](stmt.get(), a, new_local_names);
            implicit_caps.insert(nested_caps.begin(), nested_caps.end());
        }
        return implicit_caps;
    });
    AddHandler<const Parse::Identifier>(a.LambdaCaptureAnalyzers, [](const Parse::Identifier* l, Analyzer& a, std::unordered_set<Parse::Name>& local_names) {
        return std::unordered_set<Parse::Name>({ l->val });
    });
    AddHandler<const Parse::Return>(a.LambdaCaptureAnalyzers, [](const Parse::Return* r, Analyzer& a, std::unordered_set<Parse::Name>& local_names) {
        return GetLambdaCaptures(r->RetExpr.get(), a, local_names);
    });
    AddHandler<const Parse::BinaryExpression>(a.LambdaCaptureAnalyzers, [](const Parse::BinaryExpression* expr, Analyzer& a, std::unordered_set<Parse::Name>& local_names) {
        auto left = GetLambdaCaptures(expr->lhs.get(), a, local_names);
        auto rhs = GetLambdaCaptures(expr->rhs.get(), a, local_names);
        left.insert(rhs.begin(), rhs.end());
        return left;
    });
}