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

std::shared_ptr<Expression> LambdaType::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(lam, args[0]->GetType(), "operator()"));
    auto call = overset->Resolve(types, c.from);
    if (!call) overset->IssueResolutionError(types, c);
    return call->Call(std::move(args), c);
}
std::shared_ptr<Expression> LambdaType::BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    auto self = std::make_shared<ImplicitTemporaryExpr>(this, c);
    if (contents.size() == 0)
        return std::move(self);
    std::vector<std::shared_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetMembers()[i]));
        auto call = conset->Resolve(types, c.from);
        if (!call) conset->IssueResolutionError(types, c);
        // Don't PrimAccessMember because it collapses references, and DO NOT WANT
        auto obj = CreatePrimUnOp(self, types[0], [this, i](llvm::Value* val, CodegenContext& con) {
            return con.CreateStructGEP(val, boost::get<LLVMFieldIndex>(GetLocation(i)).index);
        });
        initializers.push_back(call->Call({ std::move(obj), std::move(exprs[i]) }, c));
    }
    
    struct LambdaConstruction : Expression {
        LambdaConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> inits, std::function<void(CodegenContext&)> destructor)
        : self(std::move(self)), inits(std::move(inits)), destructor(std::move(destructor)) {}
        std::vector<std::shared_ptr<Expression>> inits;
        std::shared_ptr<Expression> self;
        std::function<void(CodegenContext&)> destructor;
        Type* GetType() override final {
            return self->GetType();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            for (auto&& init : inits)
                init->GetValue(con);
            if (self->GetType()->IsComplexType())
                con.AddDestructor(destructor);
            return self->GetValue(con);
        }
    };

    return Wide::Memory::MakeUnique<LambdaConstruction>(self, std::move(initializers), BuildDestructorCall(self, c, true));
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