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
LambdaType::LambdaType(const Parse::Lambda* lam, Location l, Analyzer& a)
: AggregateType(a, l)
{
    std::unordered_map<Parse::Name, std::shared_ptr<Expression>> outer_implicit_captures;
    std::unordered_map<Parse::Name, std::shared_ptr<Expression>> inner_implicit_captures;
    std::unordered_map<Parse::Name, std::shared_ptr<Expression>> explicit_captures;
    for (auto&& arg : lam->Captures) {
        explicit_captures.insert(std::make_pair(arg.name.front().name, a.AnalyzeExpression(l, arg.initializer.get(), NonstaticLookup)));
    }
    FunctionSkeleton* skeleton;
    skeleton = a.GetWideFunction(lam, Location(l, this),
        this,
        [&](Parse::Name name, Lexer::Range where, std::function<std::shared_ptr<Expression>(Expression::InstanceKey key)> GetThis) -> std::shared_ptr<Expression> {
            auto local_skeleton = skeleton;
            if (explicit_captures.find(name) != explicit_captures.end())
                return explicit_captures[name];
            if (outer_implicit_captures.find(name) != outer_implicit_captures.end())
                return outer_implicit_captures[name];
            if (auto result = NonstaticLookup(name, where, nullptr)) {
                outer_implicit_captures[name] = result;
                inner_implicit_captures[name] = CreateResultExpression(Range::Empty(), [=, &a, &skeleton](Expression::InstanceKey key) -> std::shared_ptr<Expression> {
                    if (key == Expression::NoInstance()) return nullptr;
                    auto lambda = dynamic_cast<LambdaType*>((*key)[0]->Decay());
                    return lambda->LookupCapture(GetThis(key), name);
                });
                return inner_implicit_captures[name];
            }
            return nullptr;
        }
    );
    skeleton->ComputeBody();

    Context c(l, lam->location);
    std::vector<std::pair<Parse::Name, std::shared_ptr<Expression>>> cap_expressions;
    for (auto&& arg : lam->Captures) {
        cap_expressions.push_back(std::make_pair(arg.name.front().name, a.AnalyzeExpression(l, arg.initializer.get(), NonstaticLookup)));
    }
    for (auto&& name : outer_implicit_captures) {
        cap_expressions.push_back(std::make_pair(name.first, name.second));
    }

    std::vector<std::pair<Parse::Name, Type*>> types;
    std::vector<std::shared_ptr<Expression>> expressions;
    for (auto&& cap : cap_expressions) {
        if (!lam->defaultref)
            types.push_back(std::make_pair(cap.first, cap.second->GetType(f)->Decay()));
        else {
            if (outer_implicit_captures.find(cap.first) != outer_implicit_captures.end()) {
                if (!cap.second->GetType(f)->IsReference())
                    assert(false); // how the fuck
                types.push_back(std::make_pair(cap.first, cap.second->GetType(f)));
            } else {
                types.push_back(std::make_pair(cap.first, cap.second->GetType(f)->Decay()));
            }
        }
        expressions.push_back(std::move(cap.second));
    }

    std::size_t i = 0;
    for (auto pair : types)
        names[pair.first] = i++;
}

std::shared_ptr<Expression> LambdaType::ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType(key));
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(skeleton));
    auto call = overset->Resolve(types, c.from);
    if (!call) return overset->IssueResolutionError(types, c);
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
            auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetMembers()[i]->GetAccess(c.from));
            auto call = conset->Resolve(types, c.from);
            if (!call) return conset->IssueResolutionError(types, c);
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